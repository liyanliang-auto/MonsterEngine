// Copyright Monster Engine. All Rights Reserved.
#include "Renderer/Splat/Streaming/SplatGPUPagePool.h"
#include "Renderer/Splat/LOD/SplatSceneFormat.h"
#include "RHI/IRHICommandList.h"
#include <algorithm>
#include <cstring>
#include <deque>
#include <limits>
#include <stdexcept>

namespace MonsterRender::Splat {
namespace Profile = MonsterEngine::Profiling;
namespace {
Profile::Context PageContext(Profile::Context context, uint32_t page, uint64_t generation) {
    context.sceneId = generation;
    context.pageId = page == SplatInvalidId ? 0 : uint64_t(page) + 1;
    return context;
}
struct Range {
    uint32_t start = 0, count = 0;
};
void FreeRange(std::vector<Range>& ranges, Range r) {
    if (!r.count) return;
    ranges.push_back(r);
    std::sort(ranges.begin(), ranges.end(), [](Range a, Range b) { return a.start < b.start; });
    size_t out = 0;
    for (auto x : ranges) {
        if (out && uint64_t(ranges[out - 1].start) + ranges[out - 1].count == x.start) {
            ranges[out - 1].count += x.count;
        } else {
            ranges[out++] = x;
        }
    }
    ranges.resize(out);
}
bool Allocate(std::vector<Range>& ranges, uint32_t count, uint32_t& start, uint32_t cursor = 0) {
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < ranges.size(); ++i) {
            auto r = ranges[i];
            uint32_t candidate = pass == 0 ? std::max(r.start, cursor) : r.start;
            if (uint64_t(candidate) + count > uint64_t(r.start) + r.count) continue;
            start = candidate;
            ranges.erase(ranges.begin() + i);
            if (candidate > r.start) ranges.push_back({r.start, candidate - r.start});
            uint32_t end = candidate + count;
            if (end < uint64_t(r.start) + r.count) ranges.push_back({end, r.start + r.count - end});
            return true;
        }
    }
    return false;
}
uint64_t PayloadBytes(const FSplatPageResult& p) {
    return uint64_t(p.bytes.capacity()) + uint64_t(p.decodedGaussians.capacity()) * sizeof(FSplatGaussian) +
        p.error.capacity() + sizeof(FSplatPageResult);
}
}

struct FSplatGPUPagePool::FImpl {
    struct Page {
        uint32_t offset = 0, count = 0, recordedRows = 0;
        uint64_t generation = 0, lastUse = 0;
        bool allocated = false, ready = false, protectedCut = false;
        std::vector<RHI::FGPUCompletionToken> uses, uploads;
        uint32_t unsubmitted = 0;
        Profile::Context origin, lastUpload;
        bool firstUse=false; ///< Successful consuming submission, independent of upload completion.
        bool diagnosticObserved=false; ///< First observed successful use in an enabled capture.
        uint64_t demandSequence=0,lastSelectionSerial=0; ///< Bounded per-page re-selection identity.
        bool demandConsumed=false; ///< First successful use for this re-selection.
        Profile::Context activeDemand; ///< New already-resident demands retain their own origin.
        FSplatPageCausality causality; ///< Original physical work retained for the entire slot lifetime.
        uint64_t consumedOriginId=0; ///< Last actual upstream origin successfully consumed in this slot.
    };
    struct Staged {
        Range range;
        uint32_t page, destinationRow, rows;
        RHI::FGPUCompletionToken token;
    };
    struct Pending {
        FSplatPageResult result;
        uint64_t chargedBytes = 0;
    };
    RHI::IRHIDevice* device = nullptr;
    FSplatSceneManifest manifest;
    FSplatStreamingConfig config;
    FSplatGPUData data;
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> staging;
    std::vector<Page> pages;
    std::vector<FSplatUpstreamOrigin> upstreamOrigins; ///< Fixed scene-page bound, controller-accounted.
    std::vector<Range> freeSlots, freeStaging;
    std::deque<Pending> pending;
    std::vector<Staged> staged;
    std::vector<uint32_t> framePages;
    std::vector<std::pair<uint32_t, uint64_t>> released;
    uint64_t sceneGeneration = 0, residencyVersion = 1, nextSlotGeneration = 1, tick = 0, pendingBytes = 0,
             lastSubmitted = 0, recordingBaseline = 0;
    uint32_t stride = 0, ringCursor = 0;
    uint64_t selectionSerial=0,nextDemandSequence=0,currentSelectionVersion=0;
    FSplatGPUPagePoolStats stats;
    FSplatGPUPagePoolAllocationStats allocations;
    bool transitionAdmitted = true;
    Profile::Context recording;
    std::string recordedCutHash; ///< Hash of actual immutable active input, independent of profiling.
    std::function<bool(RHI::FGPUCompletionToken)> completionGate;
    std::shared_ptr<FSplatDetailDiagnostics> detail;
    void Detail(ESplatDetailStage stage,const Profile::Context& context,const Page& page) {
        const bool consumption=stage==ESplatDetailStage::FirstUse||stage==ESplatDetailStage::AlreadyResident||stage==ESplatDetailStage::Demand;
        if(detail)detail->Emit(stage,context,page.generation,consumption?&page.activeDemand:&page.origin,
            consumption?currentSelectionVersion:0,residencyVersion,consumption?page.demandSequence:0,&page.causality,
            consumption && context.pageId && context.pageId<=upstreamOrigins.size()?&upstreamOrigins[size_t(context.pageId-1)]:nullptr);
    }
    bool RootPage(uint32_t id) const {
        if (manifest.rootNode == SplatInvalidId) return false;
        const auto& ids = manifest.nodes[manifest.rootNode].pageIds;
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    }
    void Record(RHI::IRHICommandList* command, const Staged& chunk) {
        const auto context = PageContext(recording, chunk.page, sceneGeneration);
        Profile::Scope profileRecord("Stream/UploadCopyRecord", context, chunk.range.count);
        uint32_t widths[5] = {16, 16, 16, 4, stride - 52};
        uint32_t cursor = chunk.range.start;
        MonsterEngine::TSharedPtr<RHI::IRHIBuffer> destinations[5] = {
            data.positions, data.scales, data.rotations, data.opacities, data.shCoefficients};
        for (int k = 0; k < 5; ++k) {
            command->copyBuffer(destinations[k], staging, chunk.rows * widths[k], chunk.destinationRow * widths[k], cursor);
            cursor += chunk.rows * widths[k];
        }
    }
    bool Complete(RHI::FGPUCompletionToken t) const {
        return t.value && device->isSubmissionComplete(t) && (!completionGate || completionGate(t));
    }
    void Poll() {
        for (auto& p : pages) {
            p.uses.erase(std::remove_if(p.uses.begin(), p.uses.end(), [&](auto t) { return Complete(t); }), p.uses.end());
            p.uploads.erase(std::remove_if(p.uploads.begin(), p.uploads.end(), [&](auto t) { return Complete(t); }),
                p.uploads.end());
            if (p.allocated && !p.ready && p.recordedRows == p.count && !p.unsubmitted && p.uploads.empty()) {
                p.ready = true;
                ++residencyVersion;
                ++stats.uploadedPages;
                const auto id = uint32_t(&p - pages.data());
                Profile::EventWithContext("Stream/Resident", PageContext(p.lastUpload, id, sceneGeneration), uint64_t(p.count) * stride);
                Detail(ESplatDetailStage::GPUComplete,PageContext(p.lastUpload,id,sceneGeneration),p);
                Detail(ESplatDetailStage::ActiveReady,PageContext(p.lastUpload,id,sceneGeneration),p);
            }
        }
        for (size_t i = 0; i < staged.size();) {
            if (Complete(staged[i].token)) {
                FreeRange(freeStaging, staged[i].range);
                staged.erase(staged.begin() + i);
            } else {
                ++i;
            }
        }
    }
    bool Reserve(uint32_t count, uint32_t& offset) {
        if (Allocate(freeSlots, count, offset)) return true;
        std::vector<uint32_t> victims;
        for (uint32_t i = 0; i < pages.size(); ++i) {
            auto& p = pages[i];
            if (p.allocated && p.ready && !manifest.pages[i].pinned && !RootPage(i) && !p.protectedCut && p.uses.empty() &&
                p.uploads.empty() && !p.unsubmitted &&
                std::find(framePages.begin(), framePages.end(), i) == framePages.end()) {
                victims.push_back(i);
            }
        }
        std::sort(victims.begin(), victims.end(), [&](uint32_t a, uint32_t b) {
            return pages[a].lastUse == pages[b].lastUse ? a < b : pages[a].lastUse < pages[b].lastUse;
        });
        for (auto id : victims) {
            auto& p = pages[id];
            Profile::EventWithContext("Stream/EvictCapacity", PageContext(p.lastUpload, id, sceneGeneration), uint64_t(p.count) * stride);
            Detail(ESplatDetailStage::Evicted,PageContext(p.lastUpload,id,sceneGeneration),p);
            FreeRange(freeSlots, {p.offset, p.count});
            p = Page{};
            ++stats.evictedPages;
            ++residencyVersion;
            if (Allocate(freeSlots, count, offset)) return true;
        }
        return false;
    }
    void Release(Pending& p) {
        uint32_t id = p.result.pageId;
        uint64_t generation = p.result.sceneGeneration;
        Profile::EventWithContext("Stream/UploadPayloadReleased", PageContext(p.result.profiling, id, generation), p.chargedBytes);
        pendingBytes -= p.chargedBytes;
        FSplatPageResult empty;
        std::swap(empty, p.result); // storage dies before the lease is exposed
        {
            FSplatPageResult destroyed = std::move(empty);
        }
        released.emplace_back(id, generation);
    }
};

FSplatGPUPagePool::FSplatGPUPagePool() : Impl(std::make_unique<FImpl>()) {}

FSplatGPUPagePool::~FSplatGPUPagePool() { Shutdown(); }

bool FSplatGPUPagePool::Initialize(RHI::IRHIDevice* device, const FSplatSceneManifest& manifest,
    const FSplatStreamingConfig& config, std::string& error) {
    try {
        if (Impl->device) throw std::runtime_error("Pool already initialized; Shutdown first");
        if (!device || !device->supportsSubmissionCompletion()) {
            throw std::runtime_error("Real submission completion is required for streaming");
        }
        if (!FSplatSceneFormat::ValidateManifest(manifest, error)) throw std::runtime_error(error);
        uint32_t stride = 52 + 12 * (manifest.shDegree + 1) * (manifest.shDegree + 1);
        if (config.sceneGPUBytes < stride || config.stagingBytes < stride || config.maxUploadBytesPerFrame < stride ||
            !config.cpuCacheBytes) {
            throw std::runtime_error("Insufficient independent pool/staging/upload budgets");
        }
        auto usage = RHI::EResourceUsage::StorageBuffer | RHI::EResourceUsage::TransferDst;
        auto describe = [&](uint32_t bytes, RHI::EResourceUsage flags, bool cpu, const char* name) {
            RHI::BufferDesc desc;
            desc.size = bytes;
            desc.usage = flags;
            desc.cpuAccessible = cpu;
            desc.memoryUsage = cpu ? RHI::EMemoryUsage::Upload : RHI::EMemoryUsage::Default;
            desc.dedicatedAllocation = true;
            desc.debugName = name;
            return desc;
        };
        auto required = [&](uint32_t bytes, RHI::EResourceUsage flags, bool cpu) {
            uint64_t physical = 0, alignment = 0;
            if (!device->getBufferAllocationRequirements(describe(bytes, flags, cpu, "SplatPool_Probe"), physical,
                    alignment) ||
                physical < bytes || !alignment) {
                throw std::runtime_error("Exact backend allocation requirements unavailable");
            }
            return physical;
        };
        uint64_t allCount = 0, pinnedCount = 0;
        for (const auto& p : manifest.pages) {
            allCount += p.gaussianCount;
            bool root = manifest.rootNode != SplatInvalidId &&
                std::find(manifest.nodes[manifest.rootNode].pageIds.begin(),
                    manifest.nodes[manifest.rootNode].pageIds.end(), p.id) !=
                    manifest.nodes[manifest.rootNode].pageIds.end();
            if (root || p.pinned) pinnedCount += p.gaussianCount;
        }
        uint64_t upper = std::min<uint64_t>(
            {std::max<uint64_t>(allCount, 1), config.sceneGPUBytes / stride, UINT32_MAX / std::max<uint32_t>(16, stride - 52)});
        auto sourceBytes = [&](uint64_t count) {
            uint64_t total = 0;
            for (uint32_t width : {16u, 16u, 16u, 4u, stride - 52}) {
                uint64_t physical = required(uint32_t(count * width), usage, false);
                if (physical > config.sceneGPUBytes || total > config.sceneGPUBytes - physical) {
                    return config.sceneGPUBytes == UINT64_MAX ? UINT64_MAX : config.sceneGPUBytes + 1;
                }
                total += physical;
            }
            return total;
        };
        uint64_t low = 0, high = upper;
        while (low < high) {
            uint64_t mid = low + (high - low + 1) / 2;
            if (sourceBytes(mid) <= config.sceneGPUBytes) low = mid;
            else high = mid - 1;
        }
        uint64_t capacity = low;
        if (!capacity || capacity < pinnedCount) {
            throw std::runtime_error("Pinned root fallback cannot fit physical source budget");
        }
        for (const auto& p : manifest.pages) {
            if (p.gaussianCount > capacity) throw std::runtime_error("A single page cannot fit source budget");
        }
        low = 0;
        high = std::min<uint64_t>(config.stagingBytes, UINT32_MAX) / stride;
        while (low < high) {
            uint64_t mid = low + (high - low + 1) / 2;
            if (required(uint32_t(mid * stride), RHI::EResourceUsage::TransferSrc, true) <= config.stagingBytes) low = mid;
            else high = mid - 1;
        }
        if (!low) throw std::runtime_error("Physical staging budget cannot hold one Gaussian");
        uint32_t stagingLogical = uint32_t(low * stride);
        auto next = std::make_unique<FImpl>();
        next->device = device;
        next->manifest = manifest;
        next->config = config;
        next->stride = stride;
        next->data.gaussianCount = uint32_t(capacity);
        next->data.shDegree = int32_t(manifest.shDegree);
        next->pages.resize(manifest.pages.size());next->upstreamOrigins.resize(manifest.pages.size());
        next->freeSlots.push_back({0, uint32_t(capacity)});
        next->freeStaging.push_back({0, stagingLogical});
        auto buffer = [&](uint32_t bytes, RHI::EResourceUsage flags, bool cpu, const char* name) {
            auto desc = describe(bytes, flags, cpu, name);
            uint64_t physical=0,alignment=0;
            if(!device->getBufferAllocationRequirements(desc,physical,alignment))
                throw std::runtime_error("Allocation requirements unavailable");
            auto result = device->createBuffer(desc);
            RHI::FBufferAllocationInfo info;
            if (!result.IsValid() || !result->getAllocationInfo(info) ||
                info.reservationBytes != required(bytes, flags, cpu) || info.backingBytes != info.reservationBytes) {
                throw std::runtime_error(std::string("Cannot obtain dedicated measured allocation for ") + name);
            }
            if (cpu) {
                next->allocations.stagingLogicalBytes=bytes;
                next->allocations.stagingAlignmentBytes=alignment;
                next->allocations.stagingReservedBytes = info.reservationBytes;
                next->allocations.stagingBackingBytes = info.backingBytes;
            } else {
                next->allocations.sourceLogicalBytes+=bytes;
                next->allocations.sourceAlignmentBytes=std::max(next->allocations.sourceAlignmentBytes,alignment);
                next->allocations.sourceReservedBytes += info.reservationBytes;
                next->allocations.sourceBackingBytes += info.backingBytes;
            }
            return result;
        };
        next->data.positions = buffer(uint32_t(capacity * 16), usage, false, "SplatPool_Positions");
        next->data.scales = buffer(uint32_t(capacity * 16), usage, false, "SplatPool_Scales");
        next->data.rotations = buffer(uint32_t(capacity * 16), usage, false, "SplatPool_Rotations");
        next->data.opacities = buffer(uint32_t(capacity * 4), usage, false, "SplatPool_Opacity");
        next->data.shCoefficients = buffer(uint32_t(capacity * (stride - 52)), usage, false, "SplatPool_SH");
        next->staging = buffer(stagingLogical, RHI::EResourceUsage::TransferSrc, true, "SplatPool_StagingRing");
        if (next->allocations.sourceReservedBytes > config.sceneGPUBytes ||
            next->allocations.stagingReservedBytes > config.stagingBytes) {
            throw std::runtime_error("Actual pool reservations exceed hard budgets");
        }
        next->stats.allocatedBytes = next->allocations.sourceReservedBytes;
        Impl = std::move(next);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

bool FSplatGPUPagePool::TryQueueUpload(FSplatPageResult&& result, std::string& error) {
    auto& s = *Impl;
    auto reject = [&](const char* reason, const char* eventName) {
        // Stable, bounded reason suffix; no formatting or allocation for tracing.
        if (Profile::IsEnabled()) {
            const auto context = PageContext(result.profiling, result.pageId, result.sceneGeneration);
            Profile::EventWithContext("Stream/UploadRejected", context);
            Profile::EventWithContext(eventName, context);
        }
        error = reason;
        ++s.stats.rejectedUploads;
        s.stats.degraded = true;
        return false;
    };
    if (!s.device) return reject("Pool not initialized", "Stream/UploadRejectedNotInitialized");
    if (!result.error.empty()) return reject("Cannot upload failed page", "Stream/UploadRejectedFailedPage");
    if (result.pageId >= s.pages.size() || !result.sceneGeneration) return reject("Invalid page identity/generation", "Stream/UploadRejectedInvalid");
    if (s.sceneGeneration && result.sceneGeneration != s.sceneGeneration) return reject("Stale scene generation", "Stream/UploadRejectedStale");
    const auto& info = s.manifest.pages[result.pageId];
    if (result.decodedGaussians.size() != info.gaussianCount) return reject("Decoded count mismatch", "Stream/UploadRejectedCount");
    if (s.pages[result.pageId].allocated ||
        std::any_of(s.pending.begin(), s.pending.end(),
            [&](const auto& p) { return p.result.pageId == result.pageId; })) {
        return reject("Duplicate page upload", "Stream/UploadRejectedDuplicate");
    }
    uint64_t bytes = PayloadBytes(result);
    if (bytes > s.config.cpuCacheBytes || s.pendingBytes > s.config.cpuCacheBytes - bytes) {
        return reject("Pending CPU budget exhausted", "Stream/UploadRejectedCPUBudget");
    }
    if (!s.sceneGeneration) s.sceneGeneration = result.sceneGeneration;
    // Allocate the queue node before moving its charged payload. A failed node
    // allocation must leave the caller owning the transfer and its lease.
    s.pending.emplace_back();
    s.pending.back().result = std::move(result);
    s.pending.back().chargedBytes = bytes;
    s.pendingBytes += bytes;
    Profile::EventWithContext("Stream/UploadQueued", PageContext(s.pending.back().result.profiling,
        s.pending.back().result.pageId, s.pending.back().result.sceneGeneration), bytes);
    s.stats.degraded = false;
    error.clear();
    return true;
}

void FSplatGPUPagePool::QueueUpload(FSplatPageResult&& result) {
    std::string error;
    if (!TryQueueUpload(std::move(result), error)) {
        uint32_t id = result.pageId;
        uint64_t generation = result.sceneGeneration;
        {
            FSplatPageResult destroyed = std::move(result);
        }
        Impl->released.emplace_back(id, generation);
    }
}

void FSplatGPUPagePool::ProtectSelection(const FSplatLODSelection& selection) {
    auto& s = *Impl;
    for (auto& p : s.pages) p.protectedCut = false;
    auto protect = [&](uint32_t node) {
        if (node < s.manifest.nodes.size()) {
            for (auto id : s.manifest.nodes[node].pageIds) s.pages[id].protectedCut = true;
        }
    };
    for (auto id : selection.selectedNodeIds) protect(id);
    std::vector<bool> wanted(s.pages.size()), requested(s.pages.size());
    for (uint32_t i = 0; i < s.pages.size(); ++i) {
        wanted[i] = s.pages[i].protectedCut || s.manifest.pages[i].pinned || s.RootPage(i);
    }
    for (auto id : selection.requestPageIds) {
        if (id < wanted.size()) {
            wanted[id] = true;
            requested[id] = true;
        }
    }
    // The desired cut can lie many levels ahead. Reserve only the complete immediate
    // sibling groups being requested now, retaining already-ready siblings as well.
    for (auto node : selection.selectedNodeIds) {
        if (node < s.manifest.nodes.size()) {
            bool replacing = false;
            for (auto child : s.manifest.nodes[node].children) {
                for (auto id : s.manifest.nodes[child].pageIds) replacing = replacing || requested[id];
            }
            if (replacing) {
                for (auto child : s.manifest.nodes[node].children) {
                    for (auto id : s.manifest.nodes[child].pageIds) wanted[id] = true;
                }
            }
        }
    }
    uint64_t transitionCount = 0;
    for (uint32_t i = 0; i < wanted.size(); ++i) {
        if (wanted[i]) transitionCount += s.manifest.pages[i].gaussianCount;
    }
    s.transitionAdmitted = transitionCount <= s.data.gaussianCount;
    s.allocations.transitionRequiredRecords=transitionCount;
    s.allocations.transitionCapacityRecords=s.data.gaussianCount;
    s.allocations.transitionAdmitted=s.transitionAdmitted;
    if (s.transitionAdmitted) {
        for (uint32_t i = 0; i < wanted.size(); ++i) {
            if (wanted[i]) s.pages[i].protectedCut = true;
        }
    } else {
        s.stats.degraded = true;
    }
    // Drop obsolete unallocated CPU uploads while retaining charged ownership
    // until payload destruction. Partly recorded pages finish safely as before.
    for (auto id : selection.prefetchPageIds) {
        if (id < wanted.size()) wanted[id] = true;
    }
    for (auto it = s.pending.begin(); it != s.pending.end();) {
        const auto id = it->result.pageId;
        if (!s.pages[id].allocated && !wanted[id]) {
            Profile::EventWithContext("Stream/UploadCancelledObsolete", PageContext(it->result.profiling, id, it->result.sceneGeneration));
            if(s.detail)s.detail->Emit(ESplatDetailStage::Cancelled,PageContext(it->result.profiling,id,it->result.sceneGeneration),0,nullptr,0,0,0,&it->result.causality);
            s.Release(*it);
            it = s.pending.erase(it);
        } else {
            ++it;
        }
    }
}

void FSplatGPUPagePool::Tick(RHI::IRHICommandList* command, uint64_t generation, const Profile::Context& recordingContext) {
    auto& s = *Impl;
    s.recording = recordingContext; s.recording.submissionId = 0; s.recording.sceneId = generation;
    Profile::Scope profileTick("Stream/PoolTick", s.recording);
    if (!s.device || !command || !generation) return;
    if (s.sceneGeneration && s.sceneGeneration != generation) {
        s.stats.degraded = true;
        return;
    }
    s.sceneGeneration = generation;
    ++s.tick;
    s.Poll();
    uint64_t remaining = s.config.maxUploadBytesPerFrame;
    bool recording = false;
    for (const auto& chunk : s.staged) {
        if (!chunk.token.value) {
            if (!recording) {
                s.recordingBaseline = s.device->getLastSubmissionToken().value;
                recording = true;
            }
            s.Record(command, chunk);
            remaining = chunk.range.count > remaining ? 0 : remaining - chunk.range.count;
        }
    }
    for (auto it = s.pending.begin(); it != s.pending.end() && remaining >= s.stride;) {
        auto& result = it->result;
        auto& page = s.pages[result.pageId];
        if (!page.allocated) {
            if (!s.transitionAdmitted && !s.manifest.pages[result.pageId].pinned && !s.RootPage(result.pageId)) {
                Profile::EventWithContext("Stream/UploadRejectedTransition", PageContext(result.profiling, result.pageId, generation));
                s.Release(*it);
                it = s.pending.erase(it);
                ++s.stats.rejectedUploads;
                continue;
            }
            uint32_t offset;
            if (!s.Reserve(uint32_t(result.decodedGaussians.size()), offset)) {
                Profile::EventWithContext("Stream/UploadDeferredCapacity", PageContext(result.profiling, result.pageId, generation));
                s.stats.degraded = true;
                ++it;
                continue;
            }
            page.allocated = true;
            page.origin = PageContext(result.profiling, result.pageId, generation);
            page.causality = result.causality;
            Profile::EventWithContext("Stream/SourceAllocated", page.origin, uint64_t(result.decodedGaussians.size()) * s.stride);
            page.offset = offset;
            page.count = uint32_t(result.decodedGaussians.size());
            page.generation = s.nextSlotGeneration++;
            page.lastUse = s.tick;
            ++s.residencyVersion;
        }
        uint32_t rows = uint32_t(std::min<uint64_t>(page.count - page.recordedRows, remaining / s.stride));
        rows = std::min(rows, uint32_t(s.staging->getSize() / s.stride));
        uint32_t stagingOffset = 0;
        while (rows && !Allocate(s.freeStaging, rows * s.stride, stagingOffset, s.ringCursor)) rows /= 2;
        if (!rows) {
            Profile::EventWithContext("Stream/UploadDeferredStaging", PageContext(result.profiling, result.pageId, generation));
            ++it;
            continue;
        }
        s.ringCursor = stagingOffset + rows * s.stride;
        if (s.ringCursor >= s.staging->getSize()) s.ringCursor = 0;
        auto* mapped = static_cast<uint8_t*>(s.staging->map());
        if (!mapped) {
            FreeRange(s.freeStaging, {stagingOffset, rows * s.stride});
            s.stats.degraded = true;
            Profile::EventWithContext("Stream/UploadMapFailed", PageContext(result.profiling, result.pageId, generation));
            return;
        }
        uint32_t widths[5] = {16, 16, 16, 4, s.stride - 52};
        Profile::Scope profilePack("Stream/UploadPackAndRecord", PageContext(s.recording, result.pageId, generation), uint64_t(rows) * s.stride);
        uint32_t channelStart[5] = {};
        uint32_t cursor = stagingOffset;
        for (int k = 0; k < 5; ++k) {
            channelStart[k] = cursor;
            cursor += rows * widths[k];
        }
        for (uint32_t i = 0; i < rows; ++i) {
            const auto& g = result.decodedGaussians[page.recordedRows + i];
            const void* fields[5] = {g.position, g.scale, g.rotation, &g.opacity, g.sh};
            for (int k = 0; k < 5; ++k) {
                std::memcpy(mapped + channelStart[k] + i * widths[k], fields[k], widths[k]);
            }
        }
        s.staging->unmap();
        if (!recording) {
            s.recordingBaseline = s.device->getLastSubmissionToken().value;
            recording = true;
        }
        s.staged.push_back({{stagingOffset, rows * s.stride}, result.pageId, page.offset + page.recordedRows, rows, {}});
        s.Record(command, s.staged.back());
        s.Detail(ESplatDetailStage::UploadRecorded,PageContext(s.recording,result.pageId,generation),page);
        ++page.unsubmitted;
        page.recordedRows += rows;
        remaining -= uint64_t(rows) * s.stride;
        if (page.recordedRows == page.count) {
            s.Release(*it);
            it = s.pending.erase(it);
        } else {
            ++it;
        }
    }
    if (!s.pending.empty() && remaining < s.stride)
        Profile::EventWithContext("Stream/UploadDeferredFrameBudget", s.recording, s.pending.size());
    command->resourceBarrier();
}

FSplatResidencySnapshot FSplatGPUPagePool::GetResidency() const {
    const auto& s = *Impl;
    FSplatResidencySnapshot out;
    out.sceneGeneration = s.sceneGeneration;
    out.residencyVersion = s.residencyVersion;
    out.pages.resize(s.pages.size());
    for (uint32_t i = 0; i < s.pages.size(); ++i) {
        const auto& p = s.pages[i];
        out.pages[i] = {i, p.offset, p.count, p.generation, p.allocated && p.ready};
    }
    out.nodeReady.resize(s.manifest.nodes.size());
    for (uint32_t i = 0; i < s.manifest.nodes.size(); ++i) out.nodeReady[i] = out.IsNodeReady(s.manifest, i);
    return out;
}

FSplatActiveSet FSplatGPUPagePool::BuildActiveSet(const FSplatLODSelection& selection, bool projectedFusion,
    bool sparseFusion) {
    auto& s = *Impl;
    FSplatActiveSet out;
    s.recordedCutHash.clear();s.allocations.consumedCutHash.clear();s.allocations.consumedCutFrameId=0;s.allocations.consumedCutSubmissionToken=0;
    out.sceneGeneration = s.sceneGeneration;
    out.selectionVersion = selection.selectionVersion;
    out.residencyVersion = s.residencyVersion;
    std::vector<uint32_t> ids;
    std::vector<bool> selected(s.manifest.nodes.size());
    for (auto node : selection.selectedNodeIds) {
        if (node >= selected.size() || selected[node]) return {};
        selected[node] = true;
    }
    for (auto node : selection.selectedNodeIds) {
        auto descendants = s.manifest.nodes[node].children;
        while (!descendants.empty()) {
            auto id = descendants.back();
            descendants.pop_back();
            if (selected[id]) return {};
            descendants.insert(descendants.end(), s.manifest.nodes[id].children.begin(),
                s.manifest.nodes[id].children.end());
        }
    }
    uint64_t count = 0;
    for (auto node : selection.selectedNodeIds) {
        for (auto id : s.manifest.nodes[node].pageIds) {
            if (!s.pages[id].allocated || !s.pages[id].ready) return {};
            if (std::find(ids.begin(), ids.end(), id) != ids.end()) return {};
            ids.push_back(id);
            count += s.pages[id].count;
        }
    }
    if (count > s.data.gaussianCount || count > UINT32_MAX) return {};
    out.sourceIndices.reserve(size_t(count));
    if (s.framePages.empty() &&
        std::none_of(s.staged.begin(), s.staged.end(), [](const auto& chunk) { return !chunk.token.value; })) {
        s.recordingBaseline = s.device ? s.device->getLastSubmissionToken().value : 0;
    }
    ++s.selectionSerial;s.currentSelectionVersion=selection.selectionVersion;
    for (auto id : ids) {
        auto& page = s.pages[id];
        if(page.lastSelectionSerial+1!=s.selectionSerial||!page.demandSequence){
            page.demandSequence=++s.nextDemandSequence;page.demandConsumed=false;
            // First allocation demand joins back to its queued/read origin; later resident re-selection has a new origin.
            page.activeDemand=page.firstUse?PageContext(s.recording,id,s.sceneGeneration):page.origin;
            s.Detail(ESplatDetailStage::Demand,page.activeDemand,page);
        }
        page.lastSelectionSerial=s.selectionSerial;
        if(!Profile::IsEnabled())page.diagnosticObserved=false;
        for (uint32_t i = 0; i < page.count; ++i) out.sourceIndices.push_back(page.offset + i);
        page.lastUse = s.tick;
        if (std::find(s.framePages.begin(), s.framePages.end(), id) == s.framePages.end()) s.framePages.push_back(id);
    }
    s.allocations.selectedPageCount=ids.size();s.allocations.usedSelectedPages=0;
    s.allocations.consumedCutHash.clear();s.allocations.consumedCutFrameId=0;s.allocations.consumedCutSubmissionToken=0;
    // Bounded scratch is explicitly reserved by the controller; no GPU facts are inferred here.
    std::vector<uint8_t> cutBytes;
    cutBytes.reserve(8*(5+2*selection.selectedNodeIds.size()+4*ids.size()));
    const auto le64=[&](uint64_t value){for(unsigned i=0;i<8;++i)cutBytes.push_back(uint8_t(value>>(8*i)));};
    le64(1);le64(s.sceneGeneration);le64(selection.selectionVersion);le64(s.residencyVersion);le64(selection.selectedNodeIds.size());
    for(auto node:selection.selectedNodeIds){le64(node);le64(s.manifest.nodes[node].pageIds.size());
        for(auto id:s.manifest.nodes[node].pageIds){const auto& page=s.pages[id];le64(id);le64(page.offset);le64(page.count);le64(page.generation);}}
    s.recordedCutHash=FSplatSceneFormat::SHA256(cutBytes);
    if (projectedFusion && s.manifest.version >= 4) {
        out.fusionGroups.reserve(size_t(sparseFusion ? count / 2 : count));
        std::vector<uint32_t> mapped;
        mapped.reserve(size_t(count));
        std::vector<bool> consumed(s.manifest.nodes.size(), false);
        // A selected complete sibling partition takes precedence over optional
        // leaf-self partitions, independent of node serialization order.
        std::vector<bool> coveredByParent(s.manifest.nodes.size(), false);
        for (const auto& node : s.manifest.nodes) {
            if (!node.children.empty() && !node.originalFusionGroups.empty() &&
                std::all_of(node.children.begin(), node.children.end(),
                    [&](uint32_t child) { return selected[child]; })) {
                for (auto child : node.children) coveredByParent[child] = true;
            }
        }
        uint64_t groupBase = 0;
        for (const auto& parent : s.manifest.nodes) {
            const uint64_t firstGroup = groupBase;
            groupBase += parent.originalFusionGroups.size();
            if (groupBase >= SplatInvalidId) return {};
            if (parent.originalFusionGroups.empty()) continue;
            const auto nodeId = uint32_t(&parent - s.manifest.nodes.data());
            const bool self = parent.children.empty();
            if (self) {
                if (s.manifest.version < 5 || !selected[nodeId] || coveredByParent[nodeId]) continue;
            } else if (!std::all_of(parent.children.begin(), parent.children.end(),
                           [&](uint32_t child) { return selected[child]; })) {
                continue;
            }
            std::vector<uint32_t> rows;
            auto appendRows = [&](uint32_t node) {
                consumed[node] = true;
                for (auto pageId : s.manifest.nodes[node].pageIds) {
                    const auto& page = s.pages[pageId];
                    if (!page.ready) return false;
                    for (uint32_t i = 0; i < page.count; ++i) rows.push_back(page.offset + i);
                }
                return true;
            };
            if (self) {
                if (!appendRows(nodeId)) return {};
            } else {
                for (auto child : parent.children) {
                    if (!appendRows(child)) return {};
                }
            }
            uint32_t groupOrdinal = 0;
            for (const auto& group : parent.originalFusionGroups) {
                if (!sparseFusion || group.count >= 2) {
                    out.fusionGroups.push_back(
                        {uint32_t(mapped.size()), group.count, uint32_t(firstGroup + groupOrdinal)});
                }
                ++groupOrdinal;
                for (uint32_t i = 0; i < group.count; ++i) {
                    if (group.rows[i] >= rows.size()) return {};
                    mapped.push_back(rows[group.rows[i]]);
                }
            }
        }
        // Partial visible sibling groups and coarse fallback representations remain exact.
        for (auto nodeId : selection.selectedNodeIds) {
            if (consumed[nodeId]) continue;
            for (auto pageId : s.manifest.nodes[nodeId].pageIds) {
                const auto& page = s.pages[pageId];
                for (uint32_t i = 0; i < page.count; ++i) {
                    if (!sparseFusion) out.fusionGroups.push_back({uint32_t(mapped.size()), 1});
                    mapped.push_back(page.offset + i);
                }
            }
        }
        if (mapped.size() != count) return {};
        out.sourceIndices.swap(mapped);
    }
    return out;
}

const FSplatGPUData& FSplatGPUPagePool::GetGPUData() const { return Impl->data; }

FSplatGPUPagePoolStats FSplatGPUPagePool::GetStats() const {
    auto out = Impl->stats;
    out.residentBytes = 0;
    out.pinnedPages = 0;
    out.inFlightPages = 0;
    out.degraded = out.degraded || !Impl->transitionAdmitted;
    for (uint32_t i = 0; i < Impl->pages.size(); ++i) {
        const auto& p = Impl->pages[i];
        if (p.allocated && p.ready) out.residentBytes += uint64_t(p.count) * Impl->stride;
        if (p.allocated && (Impl->manifest.pages[i].pinned || Impl->RootPage(i) || p.protectedCut)) {
            ++out.pinnedPages;
        }
        if (p.allocated && (!p.uses.empty() || !p.uploads.empty() || p.unsubmitted ||
                               std::find(Impl->framePages.begin(), Impl->framePages.end(), i) !=
                                   Impl->framePages.end())) {
            ++out.inFlightPages;
        }
    }
    out.stagingBytes = 0;
    for (const auto& staged : Impl->staged) out.stagingBytes += staged.range.count;
    out.pendingUploads = Impl->pending.size();
    return out;
}

FSplatGPUPagePoolAllocationStats FSplatGPUPagePool::GetAllocationStats() const { return Impl->allocations; }

void FSplatGPUPagePool::MarkFrameSubmitted(RHI::FGPUCompletionToken token,bool activeConsumed) {
    auto& s = *Impl;
    if (!s.device || !token.value || token.value <= s.lastSubmitted || token.value <= s.recordingBaseline ||
        s.device->getLastSubmissionToken().value != token.value) {
        return;
    }
    s.lastSubmitted = token.value;
    for (auto& chunk : s.staged) {
        if (!chunk.token.value) {
            chunk.token = token;
            auto context = PageContext(s.recording, chunk.page, s.sceneGeneration);
            context.submissionId = token.value;
            Profile::EventWithContext("Stream/UploadSubmitted", context, chunk.range.count);
            s.Detail(ESplatDetailStage::UploadSubmitted,context,s.pages[chunk.page]);
            s.stats.uploadedBytes += chunk.range.count;
            auto& p = s.pages[chunk.page];
            p.lastUpload = context;
            if (std::none_of(p.uploads.begin(), p.uploads.end(), [&](auto t) { return t.value == token.value; })) {
                p.uploads.push_back(token);
            }
            --p.unsubmitted;
        }
    }
    for (auto id : s.framePages) {
        auto& p = s.pages[id];
        if (p.allocated) p.uses.push_back(token);
        if(p.allocated&&p.ready&&activeConsumed&&p.lastSelectionSerial==s.selectionSerial){
            ++s.allocations.usedSelectedPages;
            if(p.firstUse&&!p.diagnosticObserved&&Profile::IsEnabled()){
                auto context=PageContext(s.recording,id,s.sceneGeneration);context.submissionId=token.value;
                s.Detail(ESplatDetailStage::AlreadyResident,context,p);
            }
            const auto upstreamId=id<s.upstreamOrigins.size()?s.upstreamOrigins[id].id:0;
            if(!p.demandConsumed||(upstreamId&&p.consumedOriginId!=upstreamId)){
                auto context=PageContext(s.recording,id,s.sceneGeneration);context.submissionId=token.value;
                s.Detail(ESplatDetailStage::FirstUse,context,p);p.firstUse=true;p.demandConsumed=true;p.consumedOriginId=upstreamId;
            }
            if(Profile::IsEnabled())p.diagnosticObserved=true;
        }
        if (p.allocated && Profile::IsEnabled()) {
            auto context = PageContext(s.recording, id, s.sceneGeneration); context.submissionId = token.value;
            Profile::EventWithContext("Stream/PageUsedSubmitted", context, uint64_t(p.count) * s.stride);
        }
    }
    if(activeConsumed&&s.allocations.usedSelectedPages==s.allocations.selectedPageCount&&!s.recordedCutHash.empty()){
        s.allocations.consumedCutHash=s.recordedCutHash;s.allocations.consumedCutFrameId=s.recording.frameId;
        s.allocations.consumedCutSubmissionToken=token.value;
    }
    s.framePages.clear();
}

std::vector<std::pair<uint32_t, uint64_t>> FSplatGPUPagePool::DrainReleasedTransfers() {
    std::vector<std::pair<uint32_t, uint64_t>> out;
    out.swap(Impl->released);
    return out;
}

uint64_t FSplatGPUPagePool::GetPendingCPUBytes() const { return Impl->pendingBytes; }

bool FSplatGPUPagePool::HasPage(uint32_t id, uint64_t generation) const {
    const auto& s = *Impl;
    if (!s.device || id >= s.pages.size() || !generation || (s.sceneGeneration && generation != s.sceneGeneration)) {
        return false;
    }
    return s.pages[id].allocated || std::any_of(s.pending.begin(), s.pending.end(), [&](const auto& p) {
        return p.result.pageId == id && p.result.sceneGeneration == generation;
    });
}

void FSplatGPUPagePool::TrimUnprotected() {
    auto& s = *Impl;
    if (!s.device) return;
    s.Poll();
    for (uint32_t id = 0; id < s.pages.size(); ++id) {
        auto& page = s.pages[id];
        if (!page.allocated || page.protectedCut || s.manifest.pages[id].pinned || s.RootPage(id) ||
            !page.uses.empty() || !page.uploads.empty() || page.unsubmitted ||
            std::find(s.framePages.begin(), s.framePages.end(), id) != s.framePages.end()) continue;
        for (auto it = s.pending.begin(); it != s.pending.end();) {
            if (it->result.pageId == id) { s.Release(*it); it = s.pending.erase(it); }
            else ++it;
        }
        FreeRange(s.freeSlots, {page.offset, page.count});
        Profile::EventWithContext("Stream/EvictTrim", PageContext(page.lastUpload, id, s.sceneGeneration), uint64_t(page.count) * s.stride);
        s.Detail(ESplatDetailStage::Evicted,PageContext(page.lastUpload,id,s.sceneGeneration),page);
        page = FImpl::Page{};
        ++s.stats.evictedPages;
        ++s.residencyVersion;
    }
}

void FSplatGPUPagePool::SetCompletionGate(std::function<bool(RHI::FGPUCompletionToken)> gate) {
    Impl->completionGate = std::move(gate);
}
void FSplatGPUPagePool::SetDetailDiagnostics(std::shared_ptr<FSplatDetailDiagnostics> detail) { Impl->detail=std::move(detail); }
void FSplatGPUPagePool::SetUpstreamOrigin(uint32_t id,const FSplatUpstreamOrigin& origin) {
    auto& s=*Impl;if(id>=s.upstreamOrigins.size())return;
    const bool changed=s.upstreamOrigins[id].id!=origin.id;s.upstreamOrigins[id]=origin;
    if(changed&&origin.id&&s.pages[id].allocated&&s.pages[id].ready&&s.detail){
        const auto& page=s.pages[id];
        s.detail->Emit(ESplatDetailStage::AlreadyResident,origin.context,page.generation,nullptr,0,s.residencyVersion,0,&page.causality,&origin);
    }
}

void FSplatGPUPagePool::Shutdown() {
    if (!Impl || !Impl->device) return;
    Impl->device->waitForIdle();
    for (auto& p : Impl->pending) Impl->Release(p);
    auto released = std::move(Impl->released);
    Impl = std::make_unique<FImpl>();
    Impl->released = std::move(released);
}
}
