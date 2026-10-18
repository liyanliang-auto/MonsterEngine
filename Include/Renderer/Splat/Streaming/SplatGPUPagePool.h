// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include "Renderer/Splat/Streaming/SplatStreamingTypes.h"
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
    void Tick(RHI::IRHICommandList*,uint64_t sceneGeneration);
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
    void MarkFrameSubmitted(RHI::FGPUCompletionToken);
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
    /** Teardown may wait for device idle; ordinary Tick never waits. */
    void Shutdown();
private:
    struct FImpl;
    std::unique_ptr<FImpl> Impl;
};
}
