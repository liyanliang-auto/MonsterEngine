// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include "Renderer/Splat/Streaming/SplatStreamingTypes.h"
#include "Renderer/Splat/Streaming/SplatPageStreamer.h"
#include "Renderer/Splat/SplatPLYLoader.h"
#include "RHI/SplatGPUPrimitives.h"
#include <memory>
#include <utility>
#include <functional>
namespace MonsterRender::Splat {
/** Measured resource reservations and owning backing; each pool slab is dedicated. */
struct FSplatGPUPagePoolAllocationStats {
    uint64_t sourceReservedBytes = 0; ///< Physical source reservations, including backend padding.
    uint64_t sourceBackingBytes = 0; ///< Source backing allocations, including slack.
    uint64_t stagingReservedBytes = 0; ///< Physical upload-ring reservation.
    uint64_t stagingBackingBytes = 0; ///< Upload-ring backing allocation.
    uint64_t sourceLogicalBytes=0, stagingLogicalBytes=0; ///< Actual allocated logical slab extents.
    uint64_t sourceAlignmentBytes=0, stagingAlignmentBytes=0; ///< Maximum real queried slab alignment.
    uint64_t transitionRequiredRecords=0, transitionCapacityRecords=0; ///< Protected parent/child overlap union.
    uint64_t selectedPageCount=0, usedSelectedPages=0; ///< Last active mapping and successful consumption count.
    bool transitionAdmitted=true; ///< Observed source transition decision, independent from aggregate account.
    std::string consumedCutHash; ///< SHA256 of actual mapping; empty until successful consuming submission.
    uint64_t consumedCutFrameId=0, consumedCutSubmissionToken=0; ///< Immutable successful consumer identity.
};
/** Frame-thread-only fixed-capacity source slabs and completion-safe streaming allocator. */
class FSplatGPUPagePool {
public:
    FSplatGPUPagePool();
    ~FSplatGPUPagePool();
    FSplatGPUPagePool(const FSplatGPUPagePool&)=delete;
    FSplatGPUPagePool& operator=(const FSplatGPUPagePool&)=delete;
    /** Allocate bounded source/staging slabs; rejects unsupported completion tracking. */
    bool Initialize(RHI::IRHIDevice*,const FSplatSceneManifest&,const FSplatStreamingConfig&,std::string& error);
    /** Compatibility wrapper; use TryQueueUpload to handle backpressure without discarding data. */
    void QueueUpload(FSplatPageResult&&);
    /** Accepted results are moved; rejected results remain caller-owned and leased. */
    bool TryQueueUpload(FSplatPageResult&&,std::string& error);
    /** Protect actual/desired cuts before Tick so transitions retain complete fallback. */
    void ProtectSelection(const FSplatLODSelection&);
    /** Poll completion and record bounded uploads; performs no waits or disk reads. */
    void Tick(RHI::IRHICommandList*,uint64_t sceneGeneration,
        const MonsterEngine::Profiling::Context& recording = {});
    /** Immutable mappings only include pages whose upload submission completed. */
    FSplatResidencySnapshot GetResidency() const;
    /** Build compact source indices and pin selected slots until owning submission completes. */
    FSplatActiveSet BuildActiveSet(const FSplatLODSelection&, bool projectedFusion = false, bool sparseFusion = false);
    /** Fixed slabs; gaussianCount is allocated source capacity. */
    const FSplatGPUData& GetGPUData() const;
    /** Logical, reserved and lifecycle counters. */
    FSplatGPUPagePoolStats GetStats() const;
    /** Physical allocation telemetry independent from live staging and logical residency. */
    FSplatGPUPagePoolAllocationStats GetAllocationStats() const;
    /** Associate recordings and active mappings with a NEW successful actual submission token. */
    void MarkFrameSubmitted(RHI::FGPUCompletionToken, bool activeConsumed=true);
    /** Return transferred leases whose encoded/decoded storage has actually been destroyed. */
    std::vector<std::pair<uint32_t,uint64_t>> DrainReleasedTransfers();
    /** Account pending CPU payload independently of source/staging allocations. */
    uint64_t GetPendingCPUBytes() const;
    /** Deduplicate source work across queued, uploading and ready mappings. */
    bool HasPage(uint32_t pageId, uint64_t sceneGeneration) const;
    /** Poll real completion and retire unprotected pages, including partial uploads
     * with no outstanding commands. Call ProtectSelection first; never waits. */
    void TrimUnprotected();
    /** Optional verification/diagnostic gate can delay actual completion only.
     * Empty gate preserves real device completion. Never grants early completion. */
    void SetCompletionGate(std::function<bool(RHI::FGPUCompletionToken)> gate);
    /** Shared bounded diagnostic sink; install after Initialize, before uploads. */
    void SetDetailDiagnostics(std::shared_ptr<FSplatDetailDiagnostics>);
    /** Owner binds complete current controller-origin lifetime, even for resident pages. */
    void SetUpstreamOrigin(uint32_t pageId, const FSplatUpstreamOrigin&);
    /** Teardown may wait for device idle; ordinary Tick never waits. */
    void Shutdown();
private:
    struct FImpl;
    std::unique_ptr<FImpl> Impl;
};
}
