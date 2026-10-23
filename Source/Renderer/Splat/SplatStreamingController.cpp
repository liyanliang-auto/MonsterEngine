// Copyright Monster Engine. All Rights Reserved.
#include "Renderer/Splat/SplatStreamingController.h"
#include "Renderer/Splat/LOD/SplatSceneFormat.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace MonsterRender::Splat {
namespace {
uint64_t Add(uint64_t a, uint64_t b) {
    if (b > UINT64_MAX - a) throw std::runtime_error("Streaming admission size overflow");
    return a + b;
}
uint64_t Multiply(uint64_t a, uint64_t b) {
    if (a && b > UINT64_MAX / a) throw std::runtime_error("Streaming admission size overflow");
    return a * b;
}
uint64_t ManifestStorage(const FSplatSceneManifest& scene) {
    uint64_t bytes = sizeof(scene);
    for (const auto* text : {&scene.sourceSHA256, &scene.builderVersion, &scene.builderHash,
         &scene.configHash, &scene.buildConfig, &scene.endian, &scene.coordinates, &scene.quaternion, &scene.shLayout})
        bytes = Add(bytes, text->capacity() + 1);
    bytes = Add(bytes, Multiply(scene.nodes.capacity(), sizeof(FSplatLODNode)));
    bytes = Add(bytes, Multiply(scene.pages.capacity(), sizeof(FSplatPageInfo)));
    for (const auto& node : scene.nodes) {
        bytes = Add(bytes, Multiply(node.children.capacity() + node.pageIds.capacity(), sizeof(uint32_t)));
        bytes = Add(bytes, Multiply(node.originalFusionGroups.capacity(), sizeof(FSplatOriginalFusionGroup)));
    }
    for (const auto& page : scene.pages) bytes = Add(bytes, Add(page.relativePath.capacity() + 1, page.sha256.capacity() + 1));
    return bytes;
}
void AdmitManifest(const std::filesystem::path& path, uint64_t fileBytes,
    const FSplatLODRuntimeOptions& runtime) {
    // Inspect only the fixed schema and length prefixes before the legacy loader
    // allocates its arrays. An oversized/crafted index must fail before decoding.
    if (fileBytes < 64 || Multiply(fileBytes, 12) > runtime.streaming.cpuCacheBytes)
        throw std::runtime_error("Scene metadata decode exceeds the CPU budget");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open scene metadata for admission");
    uint64_t cursor = 0;
    const auto read32 = [&]() {
        unsigned char bytes[4];
        if (cursor > fileBytes - 4 || !input.read(reinterpret_cast<char*>(bytes), 4))
            throw std::runtime_error("Truncated scene metadata admission header");
        cursor += 4;
        return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
            (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
    };
    const auto skip = [&](uint64_t bytes) {
        if (bytes > fileBytes - cursor || !input.seekg(static_cast<std::streamoff>(bytes), std::ios::cur))
            throw std::runtime_error("Truncated scene metadata admission fields");
        cursor += bytes;
    };
    if (read32() != 0x4d53474d) throw std::runtime_error("Invalid scene metadata magic");
    const auto version = read32();
    if (version < 1 || version > 5 || read32() > 3) throw std::runtime_error("Unsupported scene metadata layout");
    for (unsigned field = 0; field < 9; ++field) {
        const auto bytes = read32();
        if (bytes > 65536) throw std::runtime_error("Scene metadata string limit exceeded");
        skip(bytes);
    }
    skip(40); // support/root, then source/leaf/proxy/data counts
    const uint64_t nodes = read32(), pages = read32();
    if (nodes > 2000000 || pages > 2000000) throw std::runtime_error("Scene metadata index limit exceeded");
    // Includes all three copies, over-allocation, parse/validation scratch, path
    // strings and optional fusion groups (whose in-memory form exceeds the wire).
    uint64_t bytes = Add(16384, Multiply(fileBytes, 12));
    bytes = Add(bytes, Multiply(nodes, 3 * sizeof(FSplatLODNode) + 512));
    bytes = Add(bytes, Multiply(pages, 3 * sizeof(FSplatPageInfo) + 256));
    bytes = Add(bytes, Multiply(runtime.activeCapacity, 2 * sizeof(uint32_t)));
    if (runtime.projectedFusion) bytes = Add(bytes, Multiply(runtime.activeCapacity, 2 * sizeof(FSplatFusionGroup)));
    if (bytes > runtime.streaming.cpuCacheBytes)
        throw std::runtime_error("Scene metadata arrays and frame workspace exceed the CPU budget");
}
bool Contained(const std::filesystem::path& root, const std::filesystem::path& path) {
    auto a = root.begin(), b = path.begin();
    for (; a != root.end(); ++a, ++b) {
        if (b == path.end()) return false;
#ifdef _WIN32
        if (_wcsicmp(a->c_str(), b->c_str()) != 0) return false;
#else
        if (*a != *b) return false;
#endif
    }
    return b != path.end();
}
bool ValidCamera(const FSplatLODView& view) {
    if (!view.width || !view.height || !std::isfinite(view.nearPlane) || !std::isfinite(view.farPlane) ||
        view.nearPlane <= 0 || view.farPlane <= view.nearPlane || !std::isfinite(view.gaussianScale) ||
        view.gaussianScale < 0.01f || view.gaussianScale > 10) return false;
    for (float x : view.view) if (!std::isfinite(x)) return false;
    for (float x : view.projection) if (!std::isfinite(x)) return false;
    for (float x : view.cameraPosition) if (!std::isfinite(x)) return false;
    return true;
}
bool ValidSettings(const FSplatLODSettings& settings) {
    return std::isfinite(settings.qualityPixels) && settings.qualityPixels > 0 &&
        std::isfinite(settings.hysteresis) && settings.hysteresis >= 0 && settings.hysteresis < 1;
}
}

struct FSplatStreamingController::FImpl {
    RHI::IRHIDevice* device = nullptr;
    FSplatSceneManifest manifest;
    FSplatLODRuntimeOptions runtime;
    FSplatLODSettings settings;
    FSplatStreamingConfig streaming;
    FSplatLODSelector selector;
    std::unique_ptr<FSplatPageStreamer> streamer;
    std::unique_ptr<FSplatGPUPagePool> pool;
    std::deque<FSplatPageResult> deferred;
    std::vector<bool> terminal;
    std::vector<uint32_t> leaves, previousCut;
    FSplatLODSelection selection;
    FSplatStreamingControllerSnapshot snapshot;
    uint64_t generation = 0, lastFrame = 0, submissionBaseline = 0, selectionVersion = 0, previousResidency = 0;
    MonsterEngine::Profiling::Context recording;
    std::shared_ptr<FSplatDetailDiagnostics> detail;
    std::vector<uint32_t> previousDemand, previousPrefetch;
    std::vector<FSplatUpstreamOrigin> upstreamOrigins; ///< Fixed manifest-page identity registry.
    std::vector<uint8_t> originRoles; ///< Fixed scratch; all desired pages, fallback and mandatory representation.
    uint64_t nextOriginId=0; ///< Monotonic within scene; retirement never reuses an id.
    bool initialized = false, cancelled = false, failed = false, pressure = false, awaitingSubmission = false;

    bool Mandatory(uint32_t page) const {
        return page < manifest.pages.size() && manifest.pages[page].pinned;
    }
    void DestroyTransfer(FSplatPageResult& result) {
        const uint32_t page = result.pageId;
        const uint64_t owner = result.sceneGeneration;
        { FSplatPageResult destroyed = std::move(result); }
        if (streamer) streamer->ReleaseTransferred(page, owner);
    }
    void ReleasePoolTransfers() {
        if (pool && streamer) for (const auto& key : pool->DrainReleasedTransfers())
            streamer->ReleaseTransferred(key.first, key.second);
    }
    void ClearDeferred() {
        for (auto& result : deferred) DestroyTransfer(result);
        deferred.clear();
    }
    void Fail(const std::string& code, const std::string& error) {
        failed = true;
        snapshot.status = {false, code, error};
        if (streamer) streamer->CancelGeneration(generation);
    }
    void Capture() {
        snapshot.desiredNodes=selection.desiredNodeIds.size(); snapshot.selectedNodes=selection.selectedNodeIds.size();
        snapshot.requestPages=selection.requestPageIds.size(); snapshot.prefetchPages=selection.prefetchPageIds.size();
        snapshot.maxSelectedProxyErrorPixels=selection.maxSelectedProxyErrorPixels;
        snapshot.sceneGeneration = generation;
        snapshot.frameId = lastFrame;
        snapshot.memoryPressure = pressure;
        snapshot.activeCount = selection.activeCount;
        snapshot.selectionVersion = selection.selectionVersion;
        snapshot.selectedLeafNodes = selection.selectedLeafNodes;
        snapshot.selectedProxyNodes = selection.selectedProxyNodes;
        snapshot.deferredPages = static_cast<uint32_t>(deferred.size());
        snapshot.terminalPageFailures = static_cast<uint32_t>(std::count(terminal.begin(), terminal.end(), true));
        if (streamer) { snapshot.io = streamer->GetStats(); snapshot.cpu = streamer->GetMemoryStats(); }
        if (pool) {
            snapshot.pool = pool->GetStats(); snapshot.allocations = pool->GetAllocationStats();
            const auto residency = pool->GetResidency();
            snapshot.residencyVersion = residency.residencyVersion;
            snapshot.rootReady = manifest.rootNode == SplatInvalidId || residency.IsNodeReady(manifest, manifest.rootNode);
        }
        snapshot.loading = !snapshot.rootReady || snapshot.io.pendingReads || snapshot.io.activeReads ||
            snapshot.io.queuedBytes || snapshot.pool.pendingUploads || snapshot.pool.stagingBytes || !deferred.empty();
        snapshot.degraded = selection.degraded || snapshot.pool.degraded || pressure || snapshot.terminalPageFailures;
        snapshot.degradationReason = pressure ? "memory_pressure_coarse_fallback" :
            snapshot.terminalPageFailures ? "fine_page_failed_ancestor_retained" : selection.degradationReason;
        snapshot.degradationCodes.clear();
        if(pressure)snapshot.degradationCodes.push_back("memory_pressure_coarse_fallback");
        if(snapshot.terminalPageFailures)snapshot.degradationCodes.push_back("fine_page_failed_ancestor_retained");
        if(snapshot.pool.degraded)snapshot.degradationCodes.push_back("pool_capacity_or_upload_degraded");
        if(!snapshot.allocations.transitionAdmitted)snapshot.degradationCodes.push_back("transition_reserve_refused");
        // Selector combines independent private explanations with '; '. Only known constants cross export.
        static constexpr std::pair<const char*,const char*> reasons[]={
            {"invalid camera or LOD settings","invalid_camera_or_lod_settings"},
            {"invalid hierarchy or representation","invalid_hierarchy_or_representation"},
            {"coarse fallback exceeds hard active/source budget","coarse_fallback_budget_exceeded"},
            {"zero sort capacity for nonempty scene","zero_sort_capacity"},
            {"frozen cut reduced by changed budgets","frozen_cut_budget_reduced"},
            {"refinement blocked by hard active/source budget","refinement_budget_blocked"},
            {"coarsening lacks parent-child transition reserve","coarsening_transition_reserve_missing"},
            {"coarse fallback loading or source mapping unavailable","coarse_fallback_not_resident"},
            {"refinement lacks parent-child transition reserve","refinement_transition_reserve_missing"},
            {"target refinement waiting for complete child residency","refinement_waiting_child_residency"},
            {"resident cut exceeds changed hard budget","resident_cut_budget_exceeded"}};
        if(selection.degraded){
            size_t start=0;
            do {
                const auto end=selection.degradationReason.find("; ",start);
                const auto text=selection.degradationReason.substr(start,end==std::string::npos?end:end-start);
                bool known=false;
                for(const auto& item:reasons)if(text==item.first){snapshot.degradationCodes.push_back(item.second);known=true;break;}
                if(!known&&text!="memory_pressure_coarse_fallback")snapshot.degradationCodes.push_back("unknown_reason");
                if(end==std::string::npos)break;start=end+2;
            }
            while(start<=selection.degradationReason.size());
        }
        if(failed)snapshot.degradationCodes.push_back("streaming_failed");
        if(cancelled)snapshot.degradationCodes.push_back("streaming_cancelled");
        detail->Drain(snapshot.detailEvents,16,snapshot.detailOverwritten,snapshot.detailDropped);
        if (failed || cancelled) { snapshot.degraded = true; snapshot.loading = false; }
        else snapshot.status = {true, snapshot.loading ? "streaming_loading" : "streaming_ready", {}};
    }
    FSplatLODSelection Coarse(const FSplatResidencySnapshot& residency) const {
        FSplatLODSelection out;
        if (manifest.rootNode == SplatInvalidId) return out;
        const uint32_t root = manifest.rootNode;
        out.desiredNodeIds.push_back(root);
        if (residency.IsNodeReady(manifest, root)) {
            out.selectedNodeIds.push_back(root); out.activeCount = manifest.nodes[root].gaussianCount;
            if (manifest.nodes[root].children.empty()) out.selectedLeafNodes = 1;
            else { out.selectedProxyNodes = 1; out.proxyGaussianCount = out.activeCount; }
        } else out.requestPageIds = manifest.nodes[root].pageIds;
        out.degraded = true; out.degradationReason = "memory_pressure_coarse_fallback";
        return out;
    }
    void Requests() {
        std::fill(originRoles.begin(),originRoles.end(),uint8_t(0));
        for(auto node:selection.desiredNodeIds)if(node<manifest.nodes.size())
            for(auto page:manifest.nodes[node].pageIds)originRoles[page]|=1;
        for(auto node:selection.selectedNodeIds)if(node<manifest.nodes.size())
            for(auto page:manifest.nodes[node].pageIds)if(!(originRoles[page]&1))originRoles[page]|=2;
        if(manifest.rootNode!=SplatInvalidId)for(auto page:manifest.nodes[manifest.rootNode].pageIds)originRoles[page]|=4;
        for(const auto& page:manifest.pages)if(page.pinned)originRoles[page.id]|=4;
        if(!pressure)for(auto page:selection.prefetchPageIds)if(page<originRoles.size())originRoles[page]|=8;
        for(size_t page=0;page<upstreamOrigins.size();++page){
            auto& origin=upstreamOrigins[page];const auto roles=originRoles[page];
            // Role changes retire the immutable old representation lifetime before a new origin.
            if(origin.id&&origin.representationFlags!=roles){
                auto event=recording;event.sceneId=generation;event.pageId=page+1;
                detail->Emit(ESplatDetailStage::UpstreamRetired,event,0,nullptr,selection.selectionVersion,snapshot.residencyVersion,0,nullptr,&origin);
                origin={};
            }
            if(roles&&!origin.id){
                origin.id=++nextOriginId;origin.context=recording;origin.context.sceneId=generation;origin.context.pageId=page+1;
                if(recording.frameId){origin.surface=snapshot.surfaceGeneration;origin.config=snapshot.configGeneration;origin.pipeline=snapshot.pipelineGeneration;}
                origin.representationFlags=roles;
                detail->Emit(ESplatDetailStage::UpstreamDemand,origin.context,0,nullptr,selection.selectionVersion,snapshot.residencyVersion,0,nullptr,&origin);
            }
            pool->SetUpstreamOrigin(uint32_t(page),origin);
        }
        std::vector<FSplatPageRequest> requests;
        std::vector<bool> seen(manifest.pages.size());
        const auto append = [&](uint32_t page, double priority) {
            if (page >= seen.size() || seen[page] || terminal[page] || pool->HasPage(page, generation)) return;
            seen[page] = true; requests.push_back({page, priority, generation});
            {
                auto& context = requests.back().profiling;
                context = recording; context.sceneId = generation; context.pageId = uint64_t(page) + 1;
                requests.back().upstream=upstreamOrigins[page];
            }
        };
        // Root priority cannot be displaced by refinement or an offscreen camera.
        if (manifest.rootNode != SplatInvalidId)
            for (auto page : manifest.nodes[manifest.rootNode].pageIds) append(page, 1e12);
        for (const auto& page : manifest.pages) if (page.pinned) append(page.id, 1e10);
        for (auto page : selection.requestPageIds) append(page, 1000);
        if (!pressure) for (auto page : selection.prefetchPageIds) append(page, 1);
        streamer->UpdateRequests(requests);
    }
    void ConsumeReady() {
        for (auto& result : streamer->DrainReady()) {
            try {
                if (failed || cancelled || result.sceneGeneration != generation) { DestroyTransfer(result); continue; }
                if (!result.error.empty()) {
                    const uint32_t page = result.pageId;
                    const std::string error = result.error;
                    DestroyTransfer(result);
                    if (page < terminal.size()) terminal[page] = true;
                    if (Mandatory(page)) Fail("required_page_failed", error);
                    continue;
                }
                if (pool->HasPage(result.pageId, generation)) { DestroyTransfer(result); continue; }
                deferred.push_back(std::move(result));
            } catch (const std::exception& error) {
                DestroyTransfer(result); Fail("streaming_transfer_failed", error.what());
            }
        }
        if (failed || cancelled) { ClearDeferred(); return; }
        for (auto it = deferred.begin(); it != deferred.end();) {
            if (pool->HasPage(it->pageId, generation)) { DestroyTransfer(*it); it = deferred.erase(it); continue; }
            std::string error;
            if (pool->TryQueueUpload(std::move(*it), error)) it = deferred.erase(it);
            else if (error == "Pending CPU budget exhausted") break; // Keep charged payload; retry next frame.
            else {
                const uint32_t page = it->pageId;
                DestroyTransfer(*it); it = deferred.erase(it);
                if (page < terminal.size()) terminal[page] = true;
                if (Mandatory(page)) { Fail("required_upload_failed", error); break; }
            }
        }
    }
};

FSplatStreamingController::FSplatStreamingController() : Impl(std::make_unique<FImpl>()) {}
FSplatStreamingController::~FSplatStreamingController() { Shutdown(); }

bool FSplatStreamingController::PrepareScene(const FSceneDescriptor& scene,
    const FSplatLODRuntimeOptions& runtime, FSplatSceneManifest& output, std::string& error) {
    try {
        const auto directory = std::filesystem::weakly_canonical(std::filesystem::u8path(scene.directory));
        const auto manifest = std::filesystem::weakly_canonical(std::filesystem::u8path(scene.manifestPath));
        if (!std::filesystem::is_directory(directory) || !Contained(directory, manifest))
            throw std::runtime_error("Manifest must be a validated file beneath the scene directory");
        AdmitManifest(manifest, std::filesystem::file_size(manifest), runtime);
        FSplatSceneManifest result;
        if (!FSplatSceneFormat::LoadManifest(manifest.string(), result, error)) return false;
        output = std::move(result); error.clear(); return true;
    } catch (const std::exception& failure) { error = failure.what(); return false; }
}

bool FSplatStreamingController::Initialize(RHI::IRHIDevice* device, const FSceneDescriptor& scene,
    const FSplatLODRuntimeOptions& runtime, std::string& error) {
    FSplatSceneManifest prepared;
    return PrepareScene(scene, runtime, prepared, error) &&
        InitializePrepared(device, scene, runtime, std::move(prepared), error);
}
bool FSplatStreamingController::InitializePrepared(RHI::IRHIDevice* device, const FSceneDescriptor& scene,
    const FSplatLODRuntimeOptions& runtime, FSplatSceneManifest prepared, std::string& error) {
    error.clear();
    if (Impl->initialized) { error = "Shutdown the previous streaming scene before replacement"; return false; }
    if (!device || !device->supportsSubmissionCompletion() || scene.kind != EViewerSceneKind::LOD ||
        !scene.generation || scene.generation <= HighestGeneration) {
        error = "Streaming requires a published LOD scene, a new generation and real GPU completion"; return false;
    }
    try {
        if (!runtime.activeCapacity || !runtime.sortCapacity || !ValidSettings(runtime.lod) || runtime.shStorageMode != 0 ||
            runtime.mode < 'B' || runtime.mode > 'E') throw std::runtime_error("Invalid bounded LOD runtime settings");
        auto next = std::make_unique<FImpl>();
        const auto directory = std::filesystem::u8path(scene.directory);
        next->manifest = std::move(prepared);
        next->device = device; next->generation = scene.generation; next->runtime = runtime;
        next->settings = runtime.lod;
        next->settings.activeBudget = std::min<uint64_t>(runtime.activeCapacity, runtime.lod.activeBudget);
        next->settings.sortBudget = std::min<uint64_t>(runtime.sortCapacity, runtime.lod.sortBudget);
        next->streaming = runtime.streaming;
        next->terminal.assign(next->manifest.pages.size(), false);
        // Small scenes do not need to allocate the maximum ring. This is storage
        // admission only: the original large Android scene keeps all 256 rows.
        const auto detailCapacity=std::max<size_t>(16,std::min<size_t>(64,next->manifest.pages.size())*4);
        next->snapshot.detailCapacity=static_cast<uint32_t>(detailCapacity);
        next->upstreamOrigins.resize(next->manifest.pages.size());next->originRoles.resize(next->manifest.pages.size());
        for (uint32_t i = 0; i < next->manifest.nodes.size(); ++i)
            if (next->manifest.nodes[i].children.empty()) next->leaves.push_back(i);
        if (next->manifest.rootNode != SplatInvalidId) {
            const auto& root = next->manifest.nodes[next->manifest.rootNode];
            if (root.gaussianCount > next->settings.activeBudget || (!next->settings.sortBudget && root.gaussianCount))
                throw std::runtime_error("Mandatory coarse representation exceeds active/sort capacity");
            for (auto page : root.pageIds) next->manifest.pages[page].pinned = true;
        }
        if (runtime.mode == 'B' || runtime.fullLeaf) {
            if (next->manifest.sourceCount > next->settings.activeBudget)
                throw std::runtime_error("Full-leaf mode exceeds active capacity");
            for (auto leaf : next->leaves) for (auto page : next->manifest.nodes[leaf].pageIds)
                next->manifest.pages[page].pinned = true;
        }
        if (runtime.mode == 'C') for (auto& page : next->manifest.pages) page.pinned = true;
        if (runtime.projectedFusion && next->manifest.version < 4)
            throw std::runtime_error("Projected fusion requires versioned original membership");
        // Three manifest owners plus conservative bounded selector/request/cut scratch
        // and two compact active mappings. All are removed from the payload budget.
        uint64_t metadata = Multiply(ManifestStorage(next->manifest), 3);
        metadata = Add(metadata, Multiply(next->manifest.nodes.size(), 512));
        metadata = Add(metadata, Multiply(next->manifest.pages.size(), 256));
        // Additional bounded causal carriers in controller/pool/results/requests, fixed event storage,
        // and canonical hash bytes. Existing payload metadata must not absorb hidden storage.
        // Include shared ownership bookkeeping and both controller objects during replacement.
        metadata = Add(metadata, sizeof(FSplatDetailDiagnostics)+Multiply(detailCapacity,sizeof(FSplatDetailEvent))+64+2*sizeof(FImpl));
        metadata = Add(metadata, Multiply(next->manifest.pages.size(),4*sizeof(FSplatUpstreamOrigin)+sizeof(FSplatPageCausality)+64));
        metadata = Add(metadata, Multiply(next->manifest.nodes.size(),16));
        metadata = Add(metadata, Multiply(runtime.activeCapacity, 2 * sizeof(uint32_t)));
        if (runtime.projectedFusion) metadata = Add(metadata, Multiply(runtime.activeCapacity, 2 * sizeof(FSplatFusionGroup)));
        if (metadata >= next->streaming.cpuCacheBytes) throw std::runtime_error("Streaming metadata and selection workspace exceed CPU budget");
        next->snapshot.controllerReservedBytes = metadata;
        next->streaming.cpuCacheBytes -= metadata;
        uint64_t minimum = 0;
        if (!FSplatPageStreamer::EstimateMinimumProgressBytes(next->manifest, next->streaming, minimum, error)) return false;
        next->snapshot.minimumProgressBytes = Add(metadata, minimum);
        if (minimum > next->streaming.cpuCacheBytes) throw std::runtime_error("CPU budget cannot sustain worker/lease minimum progress");
        next->detail=std::make_shared<FSplatDetailDiagnostics>(detailCapacity);
        next->pool = std::make_unique<FSplatGPUPagePool>();
        if (!next->pool->Initialize(device, next->manifest, next->streaming, error)) return false;
        next->pool->SetDetailDiagnostics(next->detail);
        next->settings.sourceBudget = next->pool->GetGPUData().gaussianCount;
        next->streamer = std::make_unique<FSplatPageStreamer>();
        next->streamer->SetDetailDiagnostics(next->detail);
        if (!next->streamer->Open(next->manifest, directory.string(), next->streaming, error)) return false;
        next->initialized = true;
        next->Requests(); next->Capture();
        HighestGeneration = scene.generation;
        Impl = std::move(next);
        return true;
    } catch (const std::exception& failure) { error = failure.what(); return false; }
}

FSplatStreamingFrameResult FSplatStreamingController::UpdateAndRecord(RHI::IRHICommandList* command,
    const FSplatLODView& camera, uint64_t frameId, uint64_t requestId) {
    auto& s = *Impl;
    FSplatStreamingFrameResult result;
    if (!s.initialized || s.cancelled || s.failed || !command || !frameId || frameId <= s.lastFrame || !ValidCamera(camera)) {
        result.status = s.failed || s.cancelled ? s.snapshot.status : FViewerStatus{false, "invalid_streaming_frame", "Scene, camera or frame identity is unavailable"};
        result.snapshot = std::make_shared<const FSplatStreamingControllerSnapshot>(s.snapshot);
        return result;
    }
    s.snapshot.lodSelectionMilliseconds.reset();s.snapshot.uploadRecordMilliseconds.reset();
    try {
        s.lastFrame = frameId;
        s.snapshot.detailEvents.clear();
        s.recording = {requestId, frameId, 0, s.generation, 0};
        MonsterEngine::Profiling::Scope profileUpdate("Stream/UpdateAndRecord", s.recording);
        const auto residency = s.pool->GetResidency();
        auto settings = s.settings;
        const bool leaves = s.runtime.mode == 'B' || s.runtime.fullLeaf;
        const bool fusion = s.runtime.projectedFusion && s.runtime.mode >= 'C' && !leaves;
        if (leaves || fusion) settings.qualityPixels = 0.001f;
        const auto selectionStart=std::chrono::steady_clock::now();
        s.selection = s.pressure ? s.Coarse(residency) : s.selector.Select(s.manifest, camera, settings, residency);
        if (leaves && !s.pressure && std::all_of(s.leaves.begin(), s.leaves.end(),
            [&](uint32_t node) { return residency.IsNodeReady(s.manifest, node); })) {
            s.selection = {};
            s.selection.selectedNodeIds = s.leaves; s.selection.desiredNodeIds = s.leaves;
            s.selection.activeCount = s.manifest.sourceCount; s.selection.selectedLeafNodes = s.leaves.size();
        }
        s.snapshot.lodSelectionMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-selectionStart).count();
        if (s.selection.selectedNodeIds != s.previousCut || residency.residencyVersion != s.previousResidency) ++s.selectionVersion;
        s.previousCut = s.selection.selectedNodeIds; s.previousResidency = residency.residencyVersion;
        s.selection.selectionVersion = s.selectionVersion;
        s.pool->ProtectSelection(s.selection);
        if (s.pressure) { s.streamer->OnMemoryPressure(); s.pool->TrimUnprotected(); }
        s.ReleasePoolTransfers(); s.Requests(); s.ConsumeReady();
        if (s.failed) { s.Capture(); result.status = s.snapshot.status; }
        else {
            s.submissionBaseline = s.device->getLastSubmissionToken().value;
            const auto uploadStart=std::chrono::steady_clock::now();
            s.pool->Tick(command, s.generation, s.recording);
            s.snapshot.uploadRecordMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-uploadStart).count();
            s.ReleasePoolTransfers();
            result.activeSet = s.pool->BuildActiveSet(s.selection, fusion, s.runtime.fusionImplementation == "split");
            if (result.activeSet.sourceIndices.size() != s.selection.activeCount || result.activeSet.sceneGeneration != s.generation)
                throw std::runtime_error("Resident cut coverage or generation validation failed");
            s.awaitingSubmission = true;
            s.Capture(); result.status = s.snapshot.status;
            result.uploadsRecorded = s.snapshot.pool.stagingBytes != 0;
        }
    } catch (const std::exception& failure) {
        s.Fail("streaming_record_failed", failure.what()); s.ClearDeferred(); s.Capture();
        result.activeSet = {}; result.status = s.snapshot.status;
    }
    result.snapshot = std::make_shared<const FSplatStreamingControllerSnapshot>(s.snapshot);
    return result;
}

void FSplatStreamingController::OnSubmitted(RHI::FGPUCompletionToken token,bool activeConsumed) {
    auto& s = *Impl;
    if (!s.initialized || !s.awaitingSubmission || !token.value || token.value <= s.submissionBaseline ||
        token.value <= s.snapshot.lastSubmission.value || s.device->getLastSubmissionToken().value != token.value) return;
    s.pool->MarkFrameSubmitted(token,activeConsumed&&s.snapshot.status.ok&&!s.failed&&!s.cancelled);
    s.awaitingSubmission = false;
    s.snapshot.lastSubmission = token; s.snapshot.submittedFrameId = s.lastFrame;
    s.ReleasePoolTransfers(); s.Capture();
}

void FSplatStreamingController::CancelGeneration() {
    auto& s = *Impl;
    if (!s.initialized || s.cancelled) return;
    s.cancelled = true;
    for(size_t page=0;page<s.upstreamOrigins.size();++page){auto& origin=s.upstreamOrigins[page];if(!origin.id)continue;
        MonsterEngine::Profiling::Context context{};context.sceneId=s.generation;context.pageId=page+1;
        s.detail->Emit(ESplatDetailStage::UpstreamRetired,context,0,nullptr,s.selection.selectionVersion,s.snapshot.residencyVersion,0,nullptr,&origin);
        origin={};s.pool->SetUpstreamOrigin(uint32_t(page),origin);
    }
    s.streamer->CancelGeneration(s.generation); s.ClearDeferred();
    s.snapshot.status = {false, "streaming_cancelled", "Scene generation cancelled"}; s.Capture();
}

void FSplatStreamingController::OnMemoryPressure() {
    auto& s = *Impl;
    if (!s.initialized || s.cancelled || s.failed) return;
    s.pressure = true; s.selector.Reset(); s.ClearDeferred();
    s.selection = s.Coarse(s.pool->GetResidency());
    if (s.selection.selectedNodeIds != s.previousCut) ++s.selectionVersion;
    s.previousCut = s.selection.selectedNodeIds;
    s.selection.selectionVersion = s.selectionVersion;
    s.pool->ProtectSelection(s.selection); s.pool->TrimUnprotected(); s.streamer->OnMemoryPressure();
    s.recording = {}; // Pressure-triggered work has no causal frame/request.
    s.ReleasePoolTransfers(); s.Requests(); s.Capture();
}

bool FSplatStreamingController::ApplySettings(const FSplatLODSettings& settings, std::string& error) {
    auto& s = *Impl;
    if (!s.initialized || s.cancelled || s.failed || !ValidSettings(settings) ||
        settings.activeBudget > s.runtime.activeCapacity || settings.sortBudget > s.runtime.sortCapacity ||
        (s.manifest.rootNode != SplatInvalidId && (settings.activeBudget < s.manifest.nodes[s.manifest.rootNode].gaussianCount ||
            (!settings.sortBudget && s.manifest.nodes[s.manifest.rootNode].gaussianCount)))) {
        error = "Settings require a valid scene and capacity-admitted coarse representation"; return false;
    }
    s.settings = settings; s.settings.sourceBudget = s.pool->GetGPUData().gaussianCount;
    s.pressure = false; s.selector.Reset(); error.clear(); return true;
}

FSplatStreamingControllerSnapshot FSplatStreamingController::Snapshot() const { return Impl->snapshot; }
void FSplatStreamingController::SetDiagnosticFrameIdentity(uint64_t frame,uint64_t request,uint64_t surface,uint64_t config,uint64_t pipeline) {
    Impl->snapshot.surfaceGeneration=surface;Impl->snapshot.configGeneration=config;Impl->snapshot.pipelineGeneration=pipeline;
    if(Impl->detail)Impl->detail->BindFrame({request,frame,0,Impl->generation,0},surface,config,pipeline);
}
const FSplatGPUData& FSplatStreamingController::GetGPUData() const {
    if (!Impl->initialized || !Impl->pool) throw std::runtime_error("Streaming source is unavailable");
    return Impl->pool->GetGPUData();
}
const FSplatSceneManifest& FSplatStreamingController::GetManifest() const { return Impl->manifest; }

FSplatGPUPagePool* FSplatStreamingController::GetPagePool() const { return Impl->pool.get(); }
FSplatPageStreamer* FSplatStreamingController::GetStreamer() const { return Impl->streamer.get(); }
const FSplatLODSelection& FSplatStreamingController::GetSelection() const { return Impl->selection; }

void FSplatStreamingController::Shutdown() {
    if (!Impl || !Impl->initialized) return;
    auto& s = *Impl;
    CancelGeneration();
    s.streamer->Shutdown(); // Cancels/joins reads; outstanding consumer leases stay charged.
    s.ClearDeferred();
    s.pool->Shutdown(); // Explicit teardown waits, destroys pool payloads, then exposes releases.
    s.ReleasePoolTransfers();
    const auto memory = s.streamer->GetMemoryStats();
    const bool closed = memory.totalReservedBytes == 0 && memory.transferredPages == 0;
    const uint64_t generation = s.generation;
    Impl = std::make_unique<FImpl>();
    Impl->snapshot.sceneGeneration = generation;
    Impl->snapshot.loading = false;
    Impl->snapshot.status = {closed, closed ? "streaming_stopped" : "streaming_lease_error",
        closed ? std::string{} : "CPU transfer ownership did not close during shutdown"};
}
}

