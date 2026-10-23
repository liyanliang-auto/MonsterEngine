// Copyright Monster Engine. All Rights Reserved.
#include "Renderer/Splat/SplatViewerDiagnostics.h"
#include "Profiling/MonsterProfiler.h"
#include "RHI/IRHIDevice.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanTexture.h"
#include "Platform/Vulkan/VulkanRHICommandList.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <future>
#include <iomanip>
#include <locale>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace MonsterRender::Splat {
namespace {
std::string Quote(const std::string& text) {
    std::string out = "\"";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
        else if (c >= 32 && c < 127) out += char(c);
        else { const char* h = "0123456789abcdef"; out += "\\u00"; out += h[c >> 4]; out += h[c & 15]; }
    }
    return out + '"';
}
std::string Code(const std::string& value) {
    if (value.empty() || value.size() > 96) return "redacted";
    for (unsigned char c : value) if (!(c >= 'a' && c <= 'z') &&
        !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && c != '_') return "redacted";
    return value;
}
std::string Hash(const std::string& value) {
    if (value.size() != 64 && value.size() != 40) return "null";
    for (char c : value) if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f')) return "null";
    return Quote(value);
}
template<class T> std::string Number(T value) {
    std::ostringstream out; out.imbue(std::locale::classic());
    if constexpr (std::is_floating_point_v<T>) { if (!std::isfinite(value)) return "null"; out << std::setprecision(17); }
    out << value; return out.str();
}
template<class T> std::string Optional(const std::optional<T>& value) { return value ? Number(*value) : "null"; }
struct Object {
    std::string text = "{";
    void Add(const char* key, const std::string& value) { if (text.size() > 1) text += ','; text += Quote(key) + ':' + value; }
    std::string End() const { return text + '}'; }
};
std::string Config(const FViewerConfig& c) {
    Object o; const auto& r = c.runtime; const auto& s = r.streaming;
#define FIELD(name, value) o.Add(name, Number(value))
    FIELD("generation", c.generation); o.Add("configHash", Hash(c.configHash)); o.Add("shaderIdentity", Hash(c.shaderIdentity));
    FIELD("maximumInternalEdge", c.maximumInternalEdge); FIELD("maximumInternalPixels", c.maximumInternalPixels);
    FIELD("width", r.width); FIELD("height", r.height); FIELD("renderWidth", r.renderWidth); FIELD("renderHeight", r.renderHeight);
    o.Add("mode", Quote(std::string(1, r.mode))); o.Add("vsync", r.vsync ? "true" : "false");
    FIELD("activeCapacity", r.activeCapacity); FIELD("sortCapacity", r.sortCapacity);
    FIELD("sceneGPUBytes", s.sceneGPUBytes); FIELD("stagingBytes", s.stagingBytes); FIELD("cpuCacheBytes", s.cpuCacheBytes);
    FIELD("temporaryGPUBytes", s.temporaryGPUBytes); FIELD("maxUploadBytesPerFrame", s.maxUploadBytesPerFrame);
    FIELD("maxConcurrentReads", s.maxConcurrentReads); FIELD("retries", s.retries); FIELD("retryDelayMilliseconds", s.retryDelayMilliseconds);
    FIELD("qualityPixels", r.lod.qualityPixels); FIELD("hysteresis", r.lod.hysteresis);
    FIELD("activeBudget", r.lod.activeBudget); FIELD("sortBudget", r.lod.sortBudget); FIELD("sourceBudget", r.lod.sourceBudget);
    o.Add("freeze", r.lod.freeze ? "true" : "false");
    FIELD("tightTileCulling", r.tightTileCulling); FIELD("shStorageMode", r.shStorageMode); FIELD("shStorageDegree", r.shStorageDegree);
    FIELD("displaySHDegree", r.displaySHDegree); FIELD("rasterSharedMemoryMode", r.rasterSharedMemoryMode);
    FIELD("radixSortBlocksPerWorkgroup", r.radixSortBlocksPerWorkgroup);
    o.Add("radixSortPhaseTiming", r.radixSortPhaseTiming ? "true" : "false");
    o.Add("projectedFusion", r.projectedFusion ? "true" : "false");
    o.Add("projectionColorCompression", r.projectionColorCompression ? "true" : "false");
    o.Add("fusionImplementation", Quote(r.fusionImplementation == "split" ? "split" : "scalar"));
    FIELD("fusionRelativeRGBAError", r.fusionRelativeRGBAError);
#undef FIELD
    return o.End();
}
std::string Observation(EViewerObservation value) {
    return value == EViewerObservation::Unknown ? "null" : value == EViewerObservation::Yes ? "true" : "false";
}
constexpr const char* StageNames[]={"Preprocess","Scan","AssignKeys","RadixSort","TileBounds","Render","Pipeline"};
using MeasurementFields=std::vector<std::pair<std::string,std::string>>;
MeasurementFields Fields(const FViewerDiagnosticsSnapshot& s,const FSplatStreamingControllerSnapshot* t) {
    MeasurementFields f;const auto& m=s.measurement;
    auto add=[&](const char* key,const auto& n){f.emplace_back(key,Number(n));};
    auto opt=[&](const char* key,const auto& n){f.emplace_back(key,Optional(n));};
    add("measurementSchemaVersion",2);add("pipelineGeneration",m.pipelineGeneration);
    add("ownerStartNanoseconds",m.ownerStartNanoseconds);add("presentReturnNanoseconds",m.presentReturnNanoseconds);
    opt("sessionMilliseconds",m.sessionMilliseconds);opt("acquireMilliseconds",m.acquireMilliseconds);
    opt("presentMilliseconds",m.presentMilliseconds);opt("ownerWorkMilliseconds",m.ownerWorkMilliseconds);
    add("sortStatus",m.sortStatus);opt("sortOverflow",m.sortOverflow);
    for(size_t j=0;j<7;++j){f.emplace_back(std::string("gpu")+StageNames[j]+"Milliseconds",Optional(m.gpuStages[j]));f.emplace_back(std::string("gpu")+StageNames[j]+"Status",Number(m.gpuStatus[j]));}
    add("cameraX",m.cameraPosition[0]);add("cameraY",m.cameraPosition[1]);add("cameraZ",m.cameraPosition[2]);
    add("cameraYaw",m.cameraYaw);add("cameraPitch",m.cameraPitch);add("cameraDistance",m.cameraDistance);add("externalCamera",m.externalCamera?1:0);
    add("renderWidth",s.renderWidth);add("renderHeight",s.renderHeight);add("presentationWidth",s.presentationWidth);add("presentationHeight",s.presentationHeight);add("displayWidth",s.displayWidth);add("displayHeight",s.displayHeight);
    auto stream=[&](const char* key,const auto& n){f.emplace_back(key,t?Number(n):"null");};
    opt("lodSelectionMilliseconds",t?t->lodSelectionMilliseconds:std::nullopt);opt("uploadRecordMilliseconds",t?t->uploadRecordMilliseconds:std::nullopt);
    const FSplatStreamingControllerSnapshot empty;const auto& v=t?*t:empty;
    stream("desiredNodes",v.desiredNodes);stream("selectedNodes",v.selectedNodes);stream("requestPages",v.requestPages);stream("prefetchPages",v.prefetchPages);
    stream("maxSelectedProxyErrorPixels",v.maxSelectedProxyErrorPixels);
    stream("selectedLeafNodes",v.selectedLeafNodes);stream("selectedProxyNodes",v.selectedProxyNodes);
    stream("streamPendingUploads",v.pool.pendingUploads);stream("streamReadyBytes",v.cpu.readyBytes);stream("streamDeferredPages",v.deferredPages);
    stream("streamLoading",v.loading?1:0);stream("streamRootReady",v.rootReady?1:0);stream("streamRequests",v.io.requests);
    return f;
}
std::string CauseCode(const std::string& value) {
    static constexpr const char* allowed[]={"unknown_reason","input_or_camera_unavailable","active_capacity_exceeded",
        "fusion_partition_invalid","source_mapping_invalid","mapped_input_gpu_in_flight","mapped_input_upload_failed",
        "fusion_descriptor_upload_failed","sort_capacity_overflow_previous_complete_image_retained",
        "memory_pressure_coarse_fallback","fine_page_failed_ancestor_retained","pool_capacity_or_upload_degraded",
        "transition_reserve_refused","invalid_camera_or_lod_settings","invalid_hierarchy_or_representation",
        "coarse_fallback_budget_exceeded","zero_sort_capacity","frozen_cut_budget_reduced","refinement_budget_blocked",
        "coarsening_transition_reserve_missing","coarse_fallback_not_resident","refinement_transition_reserve_missing",
        "refinement_waiting_child_residency","resident_cut_budget_exceeded","streaming_failed","streaming_cancelled"};
    for(const auto* code:allowed)if(value==code)return code;
    return "unknown_reason";
}
bool StreamMatches(const FViewerDiagnosticsSnapshot& s,const FSplatStreamingControllerSnapshot* t) {
    return t&&t->frameId==s.frameId&&t->sceneGeneration==s.sceneGeneration&&
        t->surfaceGeneration==s.surfaceGeneration&&t->configGeneration==s.configGeneration&&
        t->pipelineGeneration==s.measurement.pipelineGeneration;
}
std::string Cause(const FViewerDegradationObservation& observation) {
    Object o;const auto& status=observation.status;
    o.Add("status",Quote(status=="observed"||status=="stale_identity"?status:"unavailable"));
    std::string codes="[";std::vector<std::string> seen;
    for(size_t i=0;i<std::min<size_t>(observation.codes.size(),16);++i){
        auto code=CauseCode(observation.codes[i]);if(std::find(seen.begin(),seen.end(),code)!=seen.end())continue;
        if(codes.size()>1)codes+=',';codes+=Quote(code);seen.push_back(code);
    }
    if(codes.size()==1&&status!="observed")codes+=Quote("unknown_reason");
    o.Add("codes",codes+']');Object counters;size_t count=0;
    for(const auto& [key,value]:observation.counters){if(count++==32)break;auto code=Code(key);if(code!="redacted")counters.Add(code.c_str(),Optional(value));}
    o.Add("counters",counters.End());return o.End();
}
constexpr const char* ComponentNames[]={"CPUCache","DecodedReady","SourceGPU","StagingGPU","TemporaryGPU","Swapchain","Metadata","Builder","Java","Native"};
std::string Detail(const FSplatStreamingControllerSnapshot* stream) {
    Object o;o.Add("clockDomain","\"native_clock_monotonic_nanoseconds\"");
    o.Add("clockMapping","null");o.Add("capacity",stream&&stream->detailCapacity?Number(stream->detailCapacity):"null");
    o.Add("maximumCapacity",Number(FSplatDetailDiagnostics::Capacity));
    o.Add("perFrameExportLimit","16");o.Add("status",stream?"\"observed\"":"\"unavailable\"");
    o.Add("captureEnabled",MonsterEngine::Profiling::IsEnabled()?"true":"false");
    o.Add("overwritten",stream?Number(stream->detailOverwritten):"null");
    o.Add("dropped",stream?Number(stream->detailDropped):"null");
    o.Add("schemaVersion","2");o.Add("retryAttemptIdentity","\"unavailable_repeated_read_stages_not_unique_attempts\"");
    o.Add("unsupportedStages","[]");o.Add("historyBeforeCapture","\"unavailable\"");
    o.Add("originRegistryCapacity","64");o.Add("generationMappingStatus","\"bounded_registry_null_if_unavailable_or_ambiguous\"");
    o.Add("gpuCompleteMeaning","\"cpu_poll_observed_completion_not_gpu_timestamp\"");
    static constexpr const char* names[]={"demand","prefetch","queued","cache_hit","read_started","read_complete","cpu_ready",
        "upload_recorded","upload_submitted","gpu_complete","active_ready","first_use","cancelled","failed","evicted","already_resident","upstream_demand","upstream_retired","work_coalesced","validated","ready_transferred","work_rejected"};
    std::string events="[";
    if(stream)for(size_t i=0;i<std::min<size_t>(stream->detailEvents.size(),16);++i){
        const auto& e=stream->detailEvents[i];const auto& c=e.context;Object row;
        row.Add("sequence",Number(e.sequence));row.Add("stage",Quote(size_t(e.stage)<std::size(names)?names[size_t(e.stage)]:"unknown"));
        row.Add("timestampNanoseconds",Number(e.timestampNanoseconds));
        auto id=[&](const char* key,uint64_t n){row.Add(key,n?Number(n):"null");};
        id("sceneGeneration",c.sceneId);id("frameId",c.frameId);id("requestId",c.requestId);
        row.Add("pageId",c.pageId?Number(c.pageId-1):"null");id("submissionToken",c.submissionId);
        id("selectionVersion",e.selectionVersion);id("residencyVersion",e.residencyVersion);
        id("pageLifetime",e.pageLifetime);id("demandFrameId",e.demandFrameId);id("demandRequestId",e.demandRequestId);
        id("demandSequence",e.demandSequence);
        id("upstreamOriginId",e.upstream.id);id("workId",e.workId);id("workOriginId",e.workOriginId);
        id("originSceneGeneration",e.upstream.context.sceneId);row.Add("originPageId",e.upstream.context.pageId?Number(e.upstream.context.pageId-1):"null");
        id("originFrameId",e.upstream.context.frameId);id("originRequestId",e.upstream.context.requestId);
        id("originSurfaceGeneration",e.upstream.surface);id("originConfigGeneration",e.upstream.config);id("originPipelineGeneration",e.upstream.pipeline);
        row.Add("representationFlags",Number(e.upstream.representationFlags));
        row.Add("surfaceGeneration",Optional(e.surfaceGeneration));row.Add("configGeneration",Optional(e.configGeneration));
        row.Add("pipelineGeneration",Optional(e.pipelineGeneration));row.Add("demandSurfaceGeneration",Optional(e.demandSurfaceGeneration));
        row.Add("demandConfigGeneration",Optional(e.demandConfigGeneration));row.Add("demandPipelineGeneration",Optional(e.demandPipelineGeneration));
        if(events.size()>1)events+=',';events+=row.End();
    }
    o.Add("events",events+']');return o.End();
}
std::string DiagnosticP0(const FViewerDiagnosticsSnapshot& s,const FSplatStreamingControllerSnapshot* stream,RHI::FGPUCompletionToken token) {
    const bool matched=StreamMatches(s,stream);
    const auto* t=matched?stream:nullptr;Object o;o.Add("schemaVersion","1");Object identity;
    identity.Add("frameId",Number(s.frameId));identity.Add("submissionToken",token.value?Number(token.value):"null");
    identity.Add("sceneGeneration",Number(s.sceneGeneration));identity.Add("surfaceGeneration",Number(s.surfaceGeneration));
    identity.Add("configGeneration",Number(s.configGeneration));identity.Add("pipelineGeneration",Number(s.measurement.pipelineGeneration));
    o.Add("identity",identity.End());Object degradation;
    degradation.Add("pipeline",Cause(s.pipelineDegradation));degradation.Add("streaming",Cause(s.streamingDegradation));
    o.Add("degradation",degradation.End());Object resources;
    resources.Add("status",s.effectiveConfig?"\"observed\"":"\"unavailable\"");
    resources.Add("aggregateLimitBytes",Number(s.budget.limitBytes));resources.Add("reservedBytes",Number(s.budget.reservedBytes));resources.Add("peakBytes",Number(s.budget.peakBytes));
    Object charges;for(size_t i=0;i<std::size(ComponentNames);++i)charges.Add(ComponentNames[i],Number(s.budget.components[i]));
    resources.Add("componentCharges",charges.End());Object quotas;
    const auto quota=[&](const char* key,uint64_t requested,std::optional<uint64_t> effective,const char* unit){
        Object q;q.Add("requested",Number(requested));q.Add("effective",Optional(effective));q.Add("unit",Quote(unit));quotas.Add(key,q.End());
    };
    const auto& r=s.requestedConfig.runtime;const auto* effective=s.effectiveConfig?&s.effectiveConfig->runtime:nullptr;
    quota("source",r.streaming.sceneGPUBytes,effective?std::optional<uint64_t>(effective->streaming.sceneGPUBytes):std::nullopt,"bytes");
    quota("active",r.activeCapacity,effective?std::optional<uint64_t>(effective->activeCapacity):std::nullopt,"records");
    quota("sort",r.sortCapacity,effective?std::optional<uint64_t>(effective->sortCapacity):std::nullopt,"entries");
    quota("temporary",r.streaming.temporaryGPUBytes,effective?std::optional<uint64_t>(effective->streaming.temporaryGPUBytes):std::nullopt,"bytes");
    quota("staging",r.streaming.stagingBytes,effective?std::optional<uint64_t>(effective->streaming.stagingBytes):std::nullopt,"bytes");
    quota("cpuCache",r.streaming.cpuCacheBytes,effective?std::optional<uint64_t>(effective->streaming.cpuCacheBytes):std::nullopt,"bytes");
    resources.Add("quotas",quotas.End());Object costs;
    for(const auto* name:{"source","sort","staging","output","transition","worker","wsi","driver","temporary"}){
        FViewerResourceCost cost;auto found=s.resourceCosts.find(name);if(found!=s.resourceCosts.end())cost=found->second;
        if(std::string(name)=="wsi"&&s.presentationReservationEstimate){cost.kind="estimate";cost.estimateBytes=s.presentationReservationEstimate;cost.accounting="swapchain_charge_estimate";}
        Object c;c.Add("kind",Quote(cost.kind));c.Add("logicalBytes",Optional(cost.logicalBytes));c.Add("requiredBytes",Optional(cost.requiredBytes));
        c.Add("backingBytes",Optional(cost.backingBytes));c.Add("alignmentBytes",Optional(cost.alignmentBytes));c.Add("estimateBytes",Optional(cost.estimateBytes));
        c.Add("accounting",Quote(Code(cost.accounting)));costs.Add(name,c.End());
    }
    resources.Add("costs",costs.End());Object admission;const auto& b=s.budget;
    admission.Add("status",b.admissionSequence?(b.admissionGranted?"\"admitted\"":"\"refused\""):"\"unknown\"");
    admission.Add("reasonCodes",b.admissionSequence?'['+Quote(Code(b.admissionReason))+']':"[]");
    admission.Add("requestedBytes",b.admissionSequence?Number(b.admissionRequestedBytes):"null");
    admission.Add("availableBytes",b.admissionSequence?Number(b.admissionAvailableBytes):"null");
    const auto component=static_cast<size_t>(b.admissionComponent);
    admission.Add("component",b.admissionSequence&&component<std::size(ComponentNames)?Quote(ComponentNames[component]):"null");
    admission.Add("sequence",Number(b.admissionSequence));admission.Add("scope","\"last_aggregate_reservation_operation_not_all_possible_cuts\"");
    resources.Add("admission",admission.End());Object cut;
    const FSplatStreamingControllerSnapshot blank;const auto& v=t?*t:blank;
#define CUT(name, value) cut.Add(name,t?Number(value):"null")
    CUT("desiredNodes",v.desiredNodes);CUT("selectedNodes",v.selectedNodes);CUT("requestPages",v.requestPages);CUT("prefetchPages",v.prefetchPages);
    CUT("selectedLeafNodes",v.selectedLeafNodes);CUT("selectedProxyNodes",v.selectedProxyNodes);CUT("activeCount",v.activeCount);
    CUT("maxSelectedProxyErrorPixels",v.maxSelectedProxyErrorPixels);CUT("selectionVersion",v.selectionVersion);CUT("residencyVersion",v.residencyVersion);
    CUT("controllerReservedBytes",v.controllerReservedBytes);CUT("minimumProgressBytes",v.minimumProgressBytes);
    CUT("selectedPageCount",v.allocations.selectedPageCount);CUT("usedSelectedPages",v.allocations.usedSelectedPages);
    CUT("transitionRequiredRecords",v.allocations.transitionRequiredRecords);CUT("transitionCapacityRecords",v.allocations.transitionCapacityRecords);
#undef CUT
    cut.Add("rootReady",t?(v.rootReady?"true":"false"):"null");cut.Add("loading",t?(v.loading?"true":"false"):"null");
    cut.Add("transitionAdmitted",t?(v.allocations.transitionAdmitted?"true":"false"):"null");cut.Add("qualitySatisfied","null");
    cut.Add("consumptionSubmissionToken",t&&v.submittedFrameId==s.frameId&&v.lastSubmission.value==token.value&&token.value&&v.allocations.consumedCutSubmissionToken==token.value&&!v.allocations.consumedCutHash.empty()?Number(token.value):"null");
    const bool exactConsumed=t&&v.allocations.consumedCutFrameId==s.frameId&&v.allocations.consumedCutSubmissionToken==token.value&&token.value&&!v.allocations.consumedCutHash.empty();
    cut.Add("consumedCutHash",exactConsumed?Quote(v.allocations.consumedCutHash):"null");
    cut.Add("consumedCutHashAlgorithm","\"sha256_le64_selected_node_page_slot_v1\"");
    cut.Add("consumedCutFrameId",exactConsumed?Number(v.allocations.consumedCutFrameId):"null");
    cut.Add("consumedCutSubmissionToken",exactConsumed?Number(v.allocations.consumedCutSubmissionToken):"null");
    cut.Add("qualityEvidence","\"independent_reference_required\"");resources.Add("cut",cut.End());Object managed;
    managed.Add("liveBytes",Optional(s.vulkanManagedLiveBytes));managed.Add("backingBytes",Optional(s.vulkanManagedBackingBytes));
    managed.Add("pooledUnusedBytes",Optional(s.vulkanPooledUnusedBytes));managed.Add("accounting","\"overlaps_component_charges_not_additive\"");
    managed.Add("driverBackingBytes","null");managed.Add("processPssBytes","null");managed.Add("processRssBytes","null");
    resources.Add("vulkanManagedMemory",managed.End());o.Add("resources",resources.End());o.Add("detail",Detail(t));return o.End();
}
std::string CameraProvenance(const FViewerDiagnosticsSnapshot& s,RHI::FGPUCompletionToken token) {
    const auto& p=s.cameraProvenance;Object o;o.Add("version","1");
    const bool matched=p.recorded&&p.frameId&&token.value&&p.submissionToken==token.value&&p.frameId==s.frameId&&
        p.sceneGeneration==s.sceneGeneration&&p.surfaceGeneration==s.surfaceGeneration&&p.configGeneration==s.configGeneration&&p.pipelineGeneration==s.measurement.pipelineGeneration;
    static_assert(std::endian::native==std::endian::little,"Camera evidence encoding requires little endian");
    const bool little=std::endian::native==std::endian::little;
    const bool complete=matched&&little&&p.lineageComplete&&p.requestScope&&p.sceneGeneration&&p.surfaceGeneration&&p.configGeneration&&p.pipelineGeneration;
    const char* status=complete?"consumed":!matched?
        (p.unavailable==EViewerCameraProvenanceUnavailable::UploadOnly?"unavailable_upload_only":"unavailable_unsubmitted"):
        !p.requestScope?"unavailable_legacy":p.unavailable==EViewerCameraProvenanceUnavailable::ScopeChange?"unavailable_scope_change":"unavailable_lineage_overflow";
    o.Add("status",Quote(status));o.Add("requestScope",p.requestScope?Quote(Number(p.requestScope)):"null");
    o.Add("frameId",matched?Number(p.frameId):"null");o.Add("submissionToken",matched?Number(token.value):"null");
    o.Add("sceneGeneration",Number(p.sceneGeneration));o.Add("surfaceGeneration",Number(p.surfaceGeneration));
    o.Add("configGeneration",Number(p.configGeneration));o.Add("pipelineGeneration",Number(p.pipelineGeneration));
    o.Add("cameraRevision",Number(p.cameraRevision));o.Add("lastAppliedRequestId",p.lastAppliedRequestId?Number(p.lastAppliedRequestId):"null");
    o.Add("lastResetRequestId",p.lastResetRequestId?Number(p.lastResetRequestId):"null");
    o.Add("gestureBeginRequestId",p.gestureBeginRequestId?Number(p.gestureBeginRequestId):"null");
    o.Add("applicationGroup",p.applicationGroup?Number(p.applicationGroup):"null");
    std::string ids="[";for(size_t n=0;n<std::min<size_t>(p.contributorCount,ViewerCameraContributorCapacity);++n){if(n)ids+=',';ids+=Number(p.contributorIds[n]);}ids+=']';
    o.Add("contributorIds",ids);o.Add("contributorCount",Number(p.contributorCount));o.Add("lineageComplete",complete?"true":"false");
    const char* phase=p.phase==EViewerPointerPhase::Begin?"Begin":p.phase==EViewerPointerPhase::End?"End":p.phase==EViewerPointerPhase::Cancel?"Cancel":"Move";
    o.Add("phase",matched?Quote(phase):"\"unavailable\"");o.Add("uniformsEncoding","\"std140_le_hex176_unused_zero\"");
    std::string hex;hex.reserve(352);static constexpr char digits[]="0123456789abcdef";
    if(matched&&little)for(auto byte:p.uniforms){hex+=digits[byte>>4];hex+=digits[byte&15];}
    o.Add("uniformsHex",matched&&little?Quote(hex):"null");
    if(matched&&little){Object orbit;orbit.Add("target","["+Number(p.target[0])+","+Number(p.target[1])+","+Number(p.target[2])+"]");
        orbit.Add("yaw",Number(p.yaw));orbit.Add("pitch",Number(p.pitch));orbit.Add("distance",Number(p.distance));
        orbit.Add("external",p.externalCamera?"true":"false");o.Add("orbit",orbit.End());
    }else o.Add("orbit","null");
    auto text=o.End();if(text.size()>2048)throw std::runtime_error("Camera provenance export limit");return text;
}
std::string Snapshot(const FViewerDiagnosticsSnapshot& s, const FSplatStreamingControllerSnapshot* stream,
                     RHI::FGPUCompletionToken token, std::optional<double> cadence) {
    Object o;
    o.Add("diagnosticsP0",DiagnosticP0(s,stream,token));
    o.Add("cameraProvenance",CameraProvenance(s,token));
    o.Add("schemaVersion", "2"); o.Add("frameId", Number(s.frameId));
    Object measurement;for(const auto& field:Fields(s,stream))measurement.Add(field.first.c_str(),field.second);o.Add("measurement",measurement.End());
    o.Add("sceneGeneration", Number(s.sceneGeneration)); o.Add("surfaceGeneration", Number(s.surfaceGeneration));
    o.Add("configGeneration", Number(s.configGeneration)); o.Add("contentHash", Hash(s.contentHash));
    o.Add("buildIdentity", Hash(s.buildIdentity)); o.Add("requestedConfig", Config(s.requestedConfig));
    o.Add("effectiveConfig", s.effectiveConfig ? Config(*s.effectiveConfig) : "null");
    o.Add("submissionToken", token.value ? Number(token.value) : "null");
    o.Add("presentationFormat", Number(s.presentationFormat));
    o.Add("presentationColorSpace", Number(s.presentationColorSpace));
    o.Add("presentationFormatSRGB", s.presentationFormatSRGB ? "true" : "false");
    o.Add("renderWidth", Number(s.renderWidth)); o.Add("renderHeight", Number(s.renderHeight));
    o.Add("presentationWidth", Number(s.presentationWidth)); o.Add("presentationHeight", Number(s.presentationHeight));
    o.Add("displayWidth", Number(s.displayWidth)); o.Add("displayHeight", Number(s.displayHeight));
    o.Add("presentationDisplayScale", Number(s.presentationDisplayScale));
    o.Add("presentationSharpeningStrength", Number(s.presentationSharpeningStrength));
    o.Add("vulkanManagedLiveBytes", Optional(s.vulkanManagedLiveBytes));
    o.Add("vulkanManagedBackingBytes", Optional(s.vulkanManagedBackingBytes));
    o.Add("vulkanPooledUnusedBytes", Optional(s.vulkanPooledUnusedBytes));
    o.Add("degraded", s.degraded ? "true" : "false");
    o.Add("degradationReason", Quote(Code(s.degradationReason)));
    Object status; status.Add("ok", s.status.ok ? "true" : "false"); status.Add("code", Quote(Code(s.status.code)));
    status.Add("detail", "\"omitted_for_privacy\""); o.Add("status", status.End());
    Object counters; const auto& c = s.counters;
#define COUNT(name) counters.Add(#name, Optional(c.name))
    COUNT(activeGaussians); COUNT(sortEntries); COUNT(sortCountFrame); COUNT(residentBytes);
    COUNT(uploadedBytes); COUNT(cpuLeaseBytes); COUNT(cpuFrameMilliseconds); COUNT(gpuMilliseconds); COUNT(gpuTimingFrame);
#undef COUNT
    o.Add("counters", counters.End());
    Object timing; timing.Add("presentReturnCadenceMilliseconds", Optional(cadence));
    timing.Add("displayCadenceMilliseconds", "null"); timing.Add("videoEncodingFPS", "null");
    timing.Add("unavailableReason", "\"not_measured\"");
    timing.Add("cpuMeaning", "\"owner_cpu_interval_not_gpu\"");
    timing.Add("gpuMeaning", "\"completed_interval_original_gpuTimingFrame\"");
    timing.Add("nestedIntervalsAdditive", "false"); o.Add("timing", timing.End());
    Object budget; budget.Add("limitBytes", Number(s.budget.limitBytes)); budget.Add("reservedBytes", Number(s.budget.reservedBytes));
    budget.Add("peakBytes", Number(s.budget.peakBytes)); budget.Add("exclusiveBuild", s.budget.exclusiveBuild ? "true" : "false");
    std::string components = "["; for (auto n : s.budget.components) { if (components.size() > 1) components += ','; components += Number(n); }
    budget.Add("components", components + ']'); o.Add("budget", budget.End());
    std::string caps = "[";
    for (size_t i = 0; i < std::min<size_t>(s.capabilities.size(), 64); ++i) {
        const auto& c = s.capabilities[i]; Object item;
        item.Add("name", Quote(Code(c.name))); item.Add("supported", Observation(c.supported));
        item.Add("enabled", Observation(c.enabled)); item.Add("executed", Observation(c.executed));
        item.Add("reason", Quote(Code(c.reason))); if (i) caps += ','; caps += item.End();
    }
    o.Add("capabilities", caps + ']');
    if (StreamMatches(s,stream)) {
        Object t;
#define STREAM(name, value) t.Add(name, Number(stream->value))
        STREAM("frameId", frameId); STREAM("sceneGeneration", sceneGeneration); STREAM("submittedFrameId", submittedFrameId);
        t.Add("submissionToken", stream->lastSubmission.value ? Number(stream->lastSubmission.value) : "null");
        STREAM("activeCount", activeCount); STREAM("selectedLeafNodes", selectedLeafNodes); STREAM("selectedProxyNodes", selectedProxyNodes);
        STREAM("selectionVersion", selectionVersion); STREAM("residencyVersion", residencyVersion);
        STREAM("residentBytes", pool.residentBytes); STREAM("uploadedBytes", pool.uploadedBytes); STREAM("uploadedPages", pool.uploadedPages);
        STREAM("evictedPages", pool.evictedPages); STREAM("pendingUploads", pool.pendingUploads); STREAM("rejectedUploads", pool.rejectedUploads);
        STREAM("sourceBackingBytes", allocations.sourceBackingBytes); STREAM("stagingBackingBytes", allocations.stagingBackingBytes);
        STREAM("cpuReservedBytes", cpu.totalReservedBytes); STREAM("cpuLeaseBytes", cpu.transferredBytes);
        STREAM("cpuReadyBytes", cpu.readyBytes); STREAM("bytesRead", io.bytesRead); STREAM("requests", io.requests);
        STREAM("pendingReads", io.pendingReads); STREAM("activeReads", io.activeReads); STREAM("failedReads", io.failed);
        STREAM("terminalPageFailures", terminalPageFailures); STREAM("deferredPages", deferredPages);
#undef STREAM
        t.Add("rootReady", stream->rootReady ? "true" : "false"); t.Add("loading", stream->loading ? "true" : "false");
        o.Add("streaming", t.End());
    } else o.Add("streaming", "null");
    o.Add("unavailableReason", "\"not_observed_or_identity_not_supplied\"");
    return o.End();
}
void U32(std::vector<uint8_t>& bytes, uint32_t n) { for (int shift = 24; shift >= 0; shift -= 8) bytes.push_back(uint8_t(n >> shift)); }
uint32_t CRC(const uint8_t* data, size_t count) {
    uint32_t crc = ~0u;
    for (size_t i = 0; i < count; ++i) { crc ^= data[i]; for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u))); }
    return ~crc;
}
void Chunk(std::vector<uint8_t>& out, const char* name, const std::vector<uint8_t>& payload) {
    U32(out, uint32_t(payload.size())); const size_t begin = out.size();
    out.insert(out.end(), name, name + 4); out.insert(out.end(), payload.begin(), payload.end());
    U32(out, CRC(out.data() + begin, payload.size() + 4));
}
FViewerScreenshot Encode(std::vector<uint8_t> rgba, uint32_t width, uint32_t height, std::string metadata) {
    FViewerScreenshot result; result.metadataJSON = std::move(metadata);
    try {
        // PNG with filter 0 and bounded uncompressed DEFLATE blocks; no external codec.
        std::vector<uint8_t> raw; raw.reserve(rgba.size() + height);
        // Compute rows run bottom-to-top (positive NDC Y); presentation's negative
        // viewport height and the desktop video exporter flip this same axis.
        for (uint32_t y = 0; y < height; ++y) { raw.push_back(0); const size_t start = size_t(height - 1 - y) * width * 4;
            raw.insert(raw.end(), rgba.begin() + start, rgba.begin() + start + size_t(width) * 4); }
        std::vector<uint8_t> deflate{0x78, 0x01}; deflate.reserve(raw.size() + raw.size() / 65535 * 5 + 16);
        for (size_t at = 0; at < raw.size();) {
            const uint16_t n = uint16_t(std::min<size_t>(65535, raw.size() - at));
            deflate.push_back(at + n == raw.size() ? 1 : 0); deflate.push_back(uint8_t(n)); deflate.push_back(uint8_t(n >> 8));
            deflate.push_back(uint8_t(~n)); deflate.push_back(uint8_t((~n) >> 8));
            deflate.insert(deflate.end(), raw.begin() + at, raw.begin() + at + n); at += n;
        }
        uint32_t a = 1, b = 0; for (auto c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; } U32(deflate, b << 16 | a);
        result.png.reserve(deflate.size() + 128); result.png = {137,80,78,71,13,10,26,10};
        std::vector<uint8_t> header; U32(header, width); U32(header, height); header.insert(header.end(), {8,6,0,0,0});
        Chunk(result.png, "IHDR", header); Chunk(result.png, "IDAT", deflate); Chunk(result.png, "IEND", {});
        result.status = {true, "screenshot_completed", {}};
    } catch (const std::exception&) { result.png.clear(); result.status = {false, "screenshot_encoding_failed", {}}; }
    return result;
}
constexpr const char* CSVHeader = "frameId,sceneGeneration,surfaceGeneration,configGeneration,submissionToken,activeGaussians,sortEntries,sortCountFrame,residentBytes,uploadedBytes,cpuLeaseBytes,cpuFrameMilliseconds,gpuMilliseconds,gpuTimingFrame,presentReturnCadenceMilliseconds,degraded,streamBytesRead,streamPendingReads,streamActiveReads,streamUploadedPages,streamEvictedPages\n";
}
struct FSplatViewerDiagnostics::FImpl {
    struct Row {
        FViewerDiagnosticsSnapshot snapshot;
        std::optional<FSplatStreamingControllerSnapshot> stream;
        RHI::FGPUCompletionToken token{};
        std::optional<double> cadence;
    };
    std::mutex mutex;
    std::deque<Row> frames;
    size_t maximumFrames;
    uint64_t dropped=0, unresolvedOverwritten=0, unmatchedGPURecords=0, previousPresent=0;
    uint64_t previousSurface=0,previousScene=0,previousConfig=0,previousPipeline=0;
    RHI::IRHIDevice* captureDevice = nullptr;
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> readback;
    MonsterEngine::TSharedPtr<RHI::IRHITexture> sourceImage;
    std::optional<FSplatBudgetCoordinator::FReservation> gpuLease, cpuLease;
    RHI::FGPUCompletionToken token{}, before{};
    uint32_t width = 0, height = 0;
    std::string metadata;
    std::future<FViewerScreenshot> encoding;
    std::optional<FViewerScreenshot> ready;
};
FSplatViewerDiagnostics::FSplatViewerDiagnostics(size_t count) : Impl(std::make_unique<FImpl>()) {
    Impl->maximumFrames = std::clamp<size_t>(count, 1, 512);
}
FSplatViewerDiagnostics::~FSplatViewerDiagnostics() { Shutdown(); }
void FSplatViewerDiagnostics::Record(const FViewerDiagnosticsSnapshot& input,const FSplatStreamingControllerSnapshot* stream,
                                    RHI::FGPUCompletionToken token,uint64_t present) {
    auto& p=*Impl;std::lock_guard lock(p.mutex);auto s=input;
    // The new row contains only its own GPU data. Delayed pipeline aggregates stay
    // in session Snapshot() for compatibility, never masquerade as this row's work.
    s.counters.sortEntries.reset();s.counters.sortCountFrame.reset();
    s.counters.gpuMilliseconds.reset();s.counters.gpuTimingFrame.reset();
    const bool same=p.previousSurface==s.surfaceGeneration&&p.previousScene==s.sceneGeneration&&
        p.previousConfig==s.configGeneration&&p.previousPipeline==s.measurement.pipelineGeneration;
    std::optional<double> cadence;
    if(present&&p.previousPresent&&same&&present>p.previousPresent)cadence=double(present-p.previousPresent)/1000000.0;
    s.counters.cpuFrameMilliseconds.reset();
    if(!p.frames.empty()&&same&&s.measurement.ownerStartNanoseconds>p.frames.back().snapshot.measurement.ownerStartNanoseconds&&p.frames.back().snapshot.measurement.ownerStartNanoseconds)
        s.counters.cpuFrameMilliseconds=double(s.measurement.ownerStartNanoseconds-p.frames.back().snapshot.measurement.ownerStartNanoseconds)/1000000.0;
    if(present){p.previousPresent=present;p.previousSurface=s.surfaceGeneration;p.previousScene=s.sceneGeneration;p.previousConfig=s.configGeneration;p.previousPipeline=s.measurement.pipelineGeneration;}
    if(p.frames.size()==p.maximumFrames){
        const auto& old=p.frames.front().snapshot.measurement;
        if(old.sortStatus==0||std::find(old.gpuStatus.begin(),old.gpuStatus.end(),0u)!=old.gpuStatus.end())++p.unresolvedOverwritten;
        p.frames.pop_front();++p.dropped;
    }
    const bool matched=StreamMatches(s,stream);
    // Attach the successful original-frame observation before encoding the PNG sidecar.
    // RecordScreenshot receives pre-submit state, which cannot prove active consumption.
    if(p.readback&&p.token.value==token.value&&token.value&&!p.metadata.empty()){
        auto metadata=p.metadata;metadata.pop_back();
        metadata+=",\"pipelineGeneration\":"+Number(s.measurement.pipelineGeneration)+
            ",\"diagnosticsP0\":"+DiagnosticP0(s,matched?stream:nullptr,token)+
            ",\"viewerFrame\":"+Snapshot(s,matched?stream:nullptr,token,cadence)+'}';
        p.metadata=std::move(metadata);
    }
    p.frames.push_back({std::move(s),matched?std::optional<FSplatStreamingControllerSnapshot>(*stream):std::nullopt,token,cadence});
}
void FSplatViewerDiagnostics::ResetCadence(){auto& p=*Impl;std::lock_guard lock(p.mutex);p.previousPresent=0;p.previousSurface=p.previousScene=p.previousConfig=p.previousPipeline=0;}
void FSplatViewerDiagnostics::Resolve(uint64_t generation,const FSplatPipelineFrameStats& stats){
    MonsterEngine::Profiling::Scope scope("diagnostics.resolve");
    auto& p=*Impl;std::lock_guard lock(p.mutex);
    auto find=[&](uint64_t frame)->FImpl::Row*{
        for(auto& row:p.frames)if(row.snapshot.frameId==frame&&row.snapshot.measurement.pipelineGeneration==generation)return &row;
        ++p.unmatchedGPURecords;return nullptr;
    };
    for(const auto& sample:stats.timestampSamples){
        const auto stage=std::find_if(std::begin(StageNames),std::end(StageNames),[&](const char* label){return sample.label==label;});
        if(stage==std::end(StageNames))continue;
        if(auto* row=find(sample.frame)){
            const size_t j=stage-std::begin(StageNames);auto& m=row->snapshot.measurement;
            const bool valid=std::isfinite(sample.milliseconds)&&sample.milliseconds>=0;
            m.gpuStatus[j]=valid?1:3;m.gpuStages[j]=valid?std::optional<double>(sample.milliseconds):std::nullopt;
            if(j==6&&valid){row->snapshot.counters.gpuMilliseconds=sample.milliseconds;row->snapshot.counters.gpuTimingFrame=sample.frame;}
        }
    }
    for(const auto& sample:stats.countSamples)if(auto* row=find(sample.frame)){
        row->snapshot.measurement.sortStatus=1;row->snapshot.measurement.sortOverflow=sample.overflow?1u:0u;row->snapshot.counters.sortEntries=sample.count;row->snapshot.counters.sortCountFrame=sample.frame;
    }
}
std::string FSplatViewerDiagnostics::SnapshotJSON() const {
    MonsterEngine::Profiling::Scope scope("diagnostics.snapshot_json");
    auto& p=*Impl;std::lock_guard lock(p.mutex);Object o;
    o.Add("schemaVersion","2");o.Add("kind","\"viewer_diagnostics\"");o.Add("privacy","\"paths_and_source_omitted\"");
    o.Add("droppedFrames",Number(p.dropped));o.Add("unresolvedOverwrittenFrames",Number(p.unresolvedOverwritten));
    o.Add("unmatchedGPURecords",Number(p.unmatchedGPURecords));o.Add("capacity",Number(p.maximumFrames));
    auto encode=[](const FImpl::Row& r){return Snapshot(r.snapshot,r.stream?&*r.stream:nullptr,r.token,r.cadence);};
    o.Add("latest",p.frames.empty()?"null":encode(p.frames.back()));
    // Leave room in the existing 16 MiB transport for the profiler, CSV and platform envelope.
    // Prefer newest rows; distinguish serialization omissions from actual frame-ring eviction.
    constexpr size_t MaximumExportBytes=8ull<<20;
    size_t bytes=o.text.size(),omitted=0;std::vector<std::string> rows;
    for(auto row=p.frames.rbegin();row!=p.frames.rend();++row){
        auto encoded=encode(*row);
        if(encoded.size()+bytes+4096>MaximumExportBytes){++omitted;continue;}
        bytes+=encoded.size()+1;rows.push_back(std::move(encoded));
    }
    std::string frames="[";for(auto row=rows.rbegin();row!=rows.rend();++row){if(frames.size()>1)frames+=',';frames+=*row;}o.Add("frames",frames+']');
    o.Add("exportOmittedFrames",Number(omitted));o.Add("exportMaximumBytes",Number(MaximumExportBytes));
    o.Add("unavailableReason","\"pending_until_nonblocking_resolve_or_ring_overwrite\"");return o.End();
}
std::string FSplatViewerDiagnostics::FramesCSV() const {
    MonsterEngine::Profiling::Scope scope("diagnostics.frames_csv");
    auto& p=*Impl;std::lock_guard lock(p.mutex);std::string out=CSVHeader;out.pop_back();
    FViewerDiagnosticsSnapshot blank;
    for(const auto& f:Fields(blank,nullptr)){out+=',';out+=f.first;}out+='\n';
    for(const auto& row:p.frames){
        const auto& s=row.snapshot;const auto& c=s.counters;const auto* t=row.stream?&*row.stream:nullptr;
        std::ostringstream csv;csv.imbue(std::locale::classic());
        csv<<s.frameId<<','<<s.sceneGeneration<<','<<s.surfaceGeneration<<','<<s.configGeneration<<',';
        auto cell=[&](const auto& v){auto n=Optional(v);if(n!="null")csv<<n;csv<<',';};
        cell(row.token.value?std::optional<uint64_t>(row.token.value):std::nullopt);
        cell(c.activeGaussians);cell(c.sortEntries);cell(c.sortCountFrame);cell(c.residentBytes);cell(c.uploadedBytes);cell(c.cpuLeaseBytes);cell(c.cpuFrameMilliseconds);cell(c.gpuMilliseconds);cell(c.gpuTimingFrame);cell(row.cadence);
        csv<<(s.degraded?1:0)<<',';
        if(t)csv<<t->io.bytesRead<<','<<t->io.pendingReads<<','<<t->io.activeReads<<','<<t->pool.uploadedPages<<','<<t->pool.evictedPages;
        else csv<<",,,,";
        for(const auto& f:Fields(s,t)){csv<<',';if(f.second!="null")csv<<f.second;}csv<<'\n';out+=csv.str();
    }
    return out;
}
std::string FSplatViewerDiagnostics::BuildReportJSON(const FSplatBuildIdentity& i, const FSplatBuildProgress& p) {
    Object o; o.Add("schemaVersion", "1"); o.Add("kind", "\"build_report\"");
    o.Add("sourceSHA256", Hash(i.sourceSHA256)); o.Add("builderHash", Hash(i.builderHash)); o.Add("configHash", Hash(i.configHash));
    // Only recognized public algorithm strings may cross the privacy boundary.
    o.Add("builderVersion", i.builderVersion == SplatBoundedBuilderVersion ? Quote(SplatBoundedBuilderVersion) : "null");
    o.Add("parserVersion", i.parserVersion == SplatBuildParserVersion ? Quote(SplatBuildParserVersion) : "null");
    o.Add("buildSchemaVersion", Number(i.schemaVersion)); o.Add("sourceBytes", Number(i.sourceBytes));
    o.Add("sourceRecords", Number(i.sourceRecords)); o.Add("stage", Number(static_cast<uint32_t>(p.stage)));
#define PROGRESS(name) o.Add(#name, Number(p.name))
    PROGRESS(processedBytes); PROGRESS(processedRecords); PROGRESS(totalBytes); PROGRESS(totalRecords);
    PROGRESS(workingBytes); PROGRESS(metadataBytes); PROGRESS(peakWorkingBytes); PROGRESS(diskBytes); PROGRESS(checkpointGeneration);
#undef PROGRESS
    o.Add("hasError", p.error.empty() ? "false" : "true"); o.Add("detail", "\"omitted_for_privacy\""); return o.End();
}
bool FSplatViewerDiagnostics::RecordScreenshot(RHI::IRHIDevice& device, RHI::IRHICommandList& command,
    MonsterEngine::TSharedPtr<RHI::IRHITexture> image, const FViewerDiagnosticsSnapshot& snapshot,
    FSplatBudgetCoordinator& budget, std::string& error) {
    auto& p = *Impl;
    auto fail = [&](const char* reason) { error = reason; return false; };
    if (p.readback || p.encoding.valid() || p.ready) return fail("screenshot_busy");
    if (!device.supportsSubmissionCompletion() || !image || !snapshot.frameId) return fail("screenshot_unavailable");
    const uint32_t width = image->getWidth(), height = image->getHeight();
    const uint64_t bytes = uint64_t(width) * height * 4;
    if (!width || !height || width > 8192 || height > 8192 || bytes > (16ull << 20) ||
        image->getDesc().format != RHI::EPixelFormat::R8G8B8A8_UNORM) return fail("screenshot_extent_or_format");
    auto* list = dynamic_cast<RHI::Vulkan::FVulkanRHICommandListImmediate*>(&command);
    auto* texture = dynamic_cast<RHI::Vulkan::VulkanTexture*>(image.Get());
    if (!list || !texture || !list->GetVulkanCommandBuffer() || !list->GetVulkanCommandBuffer()->getHandle()) return fail("screenshot_backend_unavailable");
    RHI::BufferDesc desc; desc.size = bytes; desc.usage = RHI::EResourceUsage::TransferDst;
    desc.memoryUsage = RHI::EMemoryUsage::Readback; desc.cpuAccessible = true;
    desc.dedicatedAllocation = true; desc.debugName = "ViewerScreenshotReadback";
    uint64 required = 0, alignment = 0;
    if (!device.getBufferAllocationRequirements(desc, required, alignment)) return fail("screenshot_allocation_unknown");
    auto gpu = budget.TryReserve(ESplatBudgetComponent::TemporaryGPU, required, error); if (!gpu) return false;
    // RGBA, filtered input, DEFLATE and final PNG coexist. Account capacity/row overhead.
    auto cpu = budget.TryReserve(ESplatBudgetComponent::Metadata, bytes * 6 + (1ull << 20), error); if (!cpu) return false;
    auto buffer = device.createBuffer(desc); RHI::FBufferAllocationInfo allocation;
    if (!buffer || !buffer->getAllocationInfo(allocation) || allocation.backingBytes > required) return fail("screenshot_allocation_failed");
    auto* nativeBuffer = dynamic_cast<RHI::Vulkan::VulkanBuffer*>(buffer.Get()); if (!nativeBuffer) return fail("screenshot_buffer_backend");
    // Allocate metadata before recording: after the copy is recorded its resources
    // cannot retire until the actual submission or explicit owner idle/cancellation.
    Object meta; meta.Add("schemaVersion", "1"); meta.Add("frameId", Number(snapshot.frameId));
    meta.Add("sceneGeneration", Number(snapshot.sceneGeneration)); meta.Add("surfaceGeneration", Number(snapshot.surfaceGeneration));
    meta.Add("configGeneration", Number(snapshot.configGeneration)); meta.Add("contentHash", Hash(snapshot.contentHash));
    meta.Add("configHash", snapshot.effectiveConfig ? Hash(snapshot.effectiveConfig->configHash) : "null");
    meta.Add("shaderIdentity", snapshot.effectiveConfig ? Hash(snapshot.effectiveConfig->shaderIdentity) : "null");
    meta.Add("buildIdentity", Hash(snapshot.buildIdentity)); meta.Add("width", Number(width)); meta.Add("height", Number(height));
    // The compute image stores the renderer's already sRGB-encoded composite in
    // an UNORM texture. The later swapchain transfer selector is not part of this
    // image, so label the stored pixel meaning rather than the present attachment.
    meta.Add("format", "\"RGBA8_UNORM\""); meta.Add("colorSpace", "\"srgb_encoded_rgb\"");
    meta.Add("origin", "\"internal_offscreen_output\""); meta.Add("rowOrigin", "\"top_left\""); p.metadata = meta.End();
    p.before = device.getLastSubmissionToken(); p.captureDevice = &device; p.width = width; p.height = height;
    p.readback = std::move(buffer); p.sourceImage = std::move(image); p.gpuLease = std::move(gpu); p.cpuLease = std::move(cpu); p.token = {};
    auto native = list->GetVulkanCommandBuffer()->getHandle();
    VkImageMemoryBarrier before{}; before.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    before.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; before.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    before.oldLayout = VK_IMAGE_LAYOUT_GENERAL; before.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    before.srcQueueFamilyIndex = before.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    before.image = texture->getImage(); before.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCmdPipelineBarrier(native, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,nullptr,0,nullptr,1,&before);
    VkBufferImageCopy region{}; region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; region.imageExtent = {width,height,1};
    vkCmdCopyImageToBuffer(native, texture->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, nativeBuffer->getBuffer(), 1, &region);
    auto after = before; after.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; after.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    after.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; after.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    vkCmdPipelineBarrier(native,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&after);
    VkBufferMemoryBarrier host{}; host.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; host.buffer = nativeBuffer->getBuffer(); host.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(native,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
    error.clear(); return true;
}
void FSplatViewerDiagnostics::OnScreenshotSubmitted(RHI::FGPUCompletionToken token) {
    auto& p = *Impl; if (p.readback && !p.token.value && token.value > p.before.value) p.token = token;
}
void FSplatViewerDiagnostics::PollScreenshot() {
    auto& p = *Impl;
    if (p.encoding.valid() && p.encoding.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try { p.ready = p.encoding.get(); } catch (const std::exception&) { p.ready = FViewerScreenshot{{},{},{false,"screenshot_worker_failed",{}}}; }
    }
    if (!p.readback || !p.token.value || !p.captureDevice->isSubmissionComplete(p.token)) return;
    void* mapped = p.readback->map();
    if (!mapped) { p.ready = FViewerScreenshot{{}, p.metadata, {false,"screenshot_map_failed",{}}}; }
    else {
        try {
            std::vector<uint8_t> rgba(size_t(p.width) * p.height * 4);
            std::memcpy(rgba.data(), mapped, rgba.size()); p.readback->unmap(); mapped = nullptr;
            // Final metadata retains the actual readback completion token.
            auto metadata = p.metadata; metadata.pop_back(); metadata += ",\"submissionToken\":" + Number(p.token.value) + ",\"gpuCompleted\":true}";
            p.encoding = std::async(std::launch::async, Encode, std::move(rgba), p.width, p.height, std::move(metadata));
        } catch (const std::exception&) {
            if (mapped) p.readback->unmap(); p.ready = FViewerScreenshot{{},p.metadata,{false,"screenshot_worker_failed",{}}};
        }
    }
    p.readback.Reset(); p.sourceImage.Reset(); p.gpuLease.reset(); p.captureDevice = nullptr;
}
bool FSplatViewerDiagnostics::TakeScreenshot(FViewerScreenshot& result) {
    PollScreenshot(); auto& p = *Impl; if (!p.ready) return false;
    result = std::move(*p.ready); result.memoryLease = std::move(p.cpuLease); p.ready.reset(); return true;
}
void FSplatViewerDiagnostics::Shutdown() {
    if (!Impl) return; auto& p = *Impl;
    // This is the explicit teardown path, never normal frame processing.
    if (p.readback && p.captureDevice) p.captureDevice->waitForIdle();
    if (p.readback && p.token.value) PollScreenshot();
    p.readback.Reset(); p.sourceImage.Reset(); p.gpuLease.reset(); p.captureDevice = nullptr;
    if (p.encoding.valid()) { try { p.ready = p.encoding.get(); } catch (const std::exception&) { p.ready.reset(); } }
    p.ready.reset(); p.cpuLease.reset();
}
}
