// Copyright Monster Engine. All Rights Reserved.
#include "Renderer/Splat/SplatPipeline.h"
#include "Core/ShaderCompiler.h"
#include "Core/Logging/LogMacros.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace MonsterRender::Splat {
namespace {
constexpr auto StorageUsage = RHI::EResourceUsage::StorageBuffer |
    RHI::EResourceUsage::TransferDst | RHI::EResourceUsage::TransferSrc;
constexpr auto StateUsage = StorageUsage | RHI::EResourceUsage::IndirectArgs;
RHI::BufferDesc TemporaryDesc(uint64_t bytes, RHI::EResourceUsage usage,
    RHI::EMemoryUsage memory = RHI::EMemoryUsage::Default) {
    RHI::BufferDesc d;
    d.size = bytes; d.usage = usage; d.memoryUsage = memory;
    d.cpuAccessible = memory != RHI::EMemoryUsage::Default;
    d.dedicatedAllocation = true; d.debugName = "Splat_Temporary";
    return d;
}
RHI::TextureDesc OutputDesc(uint32 width, uint32 height) {
    RHI::TextureDesc d;
    d.width = width; d.height = height; d.depth = 1; d.mipLevels = 1; d.arraySize = 1;
    d.format = RHI::EPixelFormat::R8G8B8A8_UNORM;
    d.usage = RHI::EResourceUsage::UnorderedAccess | RHI::EResourceUsage::ShaderResource | RHI::EResourceUsage::TransferSrc;
    d.dedicatedAllocation = true; d.debugName = "SplatRender_Output";
    return d;
}
}

FSplatPipeline::~FSplatPipeline() = default;
bool FSplatPipeline::EnableProjectedFusion(bool split) {
    if (m_bInitialized) return false;
    m_fusionEnabled = true; m_splitFusionEnabled = split; return true;
}
bool FSplatPipeline::SetFusionGroups(const std::vector<FSplatFusionGroup>& groups) {
    if (!groups.empty() && !m_fusionEnabled) return false;
    if (m_splitFusionEnabled) return m_splitFusion.SetGroups(groups);
    uint64_t next = 0;
    for (const auto& group : groups) {
        if (group.First() != next || !group.Count() ||
            next + group.Count() > m_activeCapacity) return false;
        next += group.Count();
    }
    m_fusionGroups = groups; return true;
}
void FSplatPipeline::SetCapacityBudgets(uint32 source, uint32 active, uint32 sort, uint64_t bytes) {
    if (m_bInitialized) {
        m_stats.degraded = true; m_stats.degradationReason = "Capacity changes require a new pipeline after completion"; return;
    }
    m_sourceCapacity = source; m_activeCapacity = active; m_sortCapacity = sort;
    m_temporaryBudget = bytes; m_explicitBudgets = true;
}
bool FSplatPipeline::PreflightTemporaryBuffers(uint64_t& bytes) {
    bytes = 0;
    auto add = [&](uint64_t size, RHI::EResourceUsage usage,
        RHI::EMemoryUsage memory = RHI::EMemoryUsage::Default) {
        uint64_t required = 0, alignment = 0;
        if (!m_device->getBufferAllocationRequirements(TemporaryDesc(size, usage, memory), required, alignment) ||
            !required || required > m_temporaryBudget - bytes) return false;
        bytes += required; return true;
    };
    const uint64_t n = m_activeCapacity, s = m_sortCapacity;
    // Compact outputs and two inclusive scan arrays; no assign copy arrays.
    for (uint32 width : {4u, 4u, 16u, 16u, 8u, 4u, 16u, 4u, 4u}) if (!add(n * width, StorageUsage)) return false;
    for (uint32 i = 0; i < 2; ++i)
        if (!add(sizeof(FCameraUniforms), RHI::EResourceUsage::UniformBuffer, RHI::EMemoryUsage::Dynamic) ||
            !add(n * 4, RHI::EResourceUsage::StorageBuffer, RHI::EMemoryUsage::Dynamic)) return false;
    if (m_splitFusionEnabled) {
        uint64_t required=0;
        if (!FSplatSplitFusionPass::Preflight(m_device,m_activeCapacity,required)||required>m_temporaryBudget-bytes) return false;
        bytes+=required;
    } else if (m_fusionEnabled) {
        for (uint32 i = 0; i < 2; ++i)
            if (!add(n * sizeof(FSplatFusionGroup) + 16, RHI::EResourceUsage::StorageBuffer |
                RHI::EResourceUsage::TransferSrc, RHI::EMemoryUsage::Dynamic)) return false;
        if (!add(16, StorageUsage) || !add(n * 16, StorageUsage)) return false;
    }
    for (uint32 width : {8u, 8u, 4u, 4u}) if (!add(s * width, StorageUsage)) return false;
    if (!add(((s + 8191) / 8192) * RADIX_SORT_BINS * 4, StorageUsage) ||
        !add(uint64_t(m_gridX) * m_gridY * 8, RHI::EResourceUsage::StorageBuffer | RHI::EResourceUsage::TransferDst) ||
        !add(ESplatDispatchState::Bytes, StateUsage)) return false;
    for (uint32 i = 0; i < 3; ++i)
        if (!add(ESplatDispatchState::Bytes, RHI::EResourceUsage::TransferDst, RHI::EMemoryUsage::Readback)) return false;
    uint64_t required = 0, alignment = 0;
    if (!m_device->getTextureAllocationRequirements(OutputDesc(m_imageWidth, m_imageHeight), required, alignment) ||
        !required || required > m_temporaryBudget - bytes) return false;
    bytes += required;
    return true;
}
bool FSplatPipeline::MeasureTemporaryBuffers(uint64_t& bytes) const {
    auto buffers = m_preprocess.GetOwnedBuffers();
    for (auto b : m_prefixSum.GetOwnedBuffers()) buffers.push_back(b);
    for (auto b : m_radixSort.GetOwnedBuffers()) buffers.push_back(b);
    buffers.push_back(m_tileBoundaries.getTileRanges()); buffers.push_back(m_dispatchState);
    for (const auto& slot : m_telemetry) buffers.push_back(slot.buffer);
    if (m_splitFusionEnabled) {
        for(auto b:m_splitFusion.GetOwnedBuffers())buffers.push_back(b);
    } else if (m_fusionEnabled) {
        buffers.push_back(m_fusionGroupsBuffers[0]); buffers.push_back(m_fusionGroupsBuffers[1]);
        buffers.push_back(m_fusionCounter); buffers.push_back(m_fusionCache);
    }
    bytes = 0; std::set<uint64_t> seen;
    auto add = [&](const RHI::FBufferAllocationInfo& info) {
        if (!info.backingId || !info.backingBytes) return false;
        if (seen.insert(info.backingId).second) {
            if (info.backingBytes > m_temporaryBudget - bytes) return false;
            bytes += info.backingBytes;
        }
        return true;
    };
    for (const auto& b : buffers) {
        RHI::FBufferAllocationInfo info{};
        if (!b || !b->getAllocationInfo(info) || !add(info)) return false;
    }
    RHI::FBufferAllocationInfo image{};
    return m_render.getOutputTexture()->getAllocationInfo(image) && add(image);
}
bool FSplatPipeline::initialize(RHI::IRHIDevice* device, uint32 count, uint32 width, uint32 height, uint32 estimateTiles) {
    if (m_bInitialized || !device || !width || !height || width > 65535u * 16u || height > 65535u * 16u ||
        !device->supportsDispatchIndirect() || !device->supportsSubmissionCompletion()) return false;
    m_device = device; m_gaussianCount = count; m_imageWidth = width; m_imageHeight = height;
    m_gridX = (width + 15u) / 16u; m_gridY = (height + 15u) / 16u;
    if (uint64_t(m_gridX) * m_gridY > UINT32_MAX / 8u) return false;
    if (!m_explicitBudgets) {
        m_sourceCapacity = std::max(1u, count); m_activeCapacity = std::max(1u, count);
        m_sortCapacity = uint32(std::max<uint64_t>(1, std::min<uint64_t>(uint64_t(count) * std::max(1u, estimateTiles), 16ull * 1024 * 1024)));
    }
    if (!m_sourceCapacity || m_sourceCapacity > UINT32_MAX / 192u || !m_activeCapacity || !m_sortCapacity || !m_temporaryBudget || count > m_sourceCapacity ||
        uint64_t(m_activeCapacity) * 16 > UINT32_MAX || m_sortCapacity > 65535u * 8192u) return false;
    uint64_t bytes = 0;
    if (!m_explicitBudgets) while (!PreflightTemporaryBuffers(bytes) && m_sortCapacity > 1)
        m_sortCapacity = std::max(1u, m_sortCapacity / 2);
    if (!PreflightTemporaryBuffers(bytes)) {
        m_stats.degraded = true; m_stats.degradationReason = "Physical temporary memory requirements exceed budget or are unavailable"; return false;
    }
    m_dispatchState = device->createBuffer(TemporaryDesc(ESplatDispatchState::Bytes, StateUsage));
    if (!m_dispatchState || !m_preprocess.initialize(device, m_activeCapacity) || !m_prefixSum.initialize(device, m_activeCapacity) ||
        !m_radixSort.initialize(device, m_sortCapacity, m_dispatchState) ||
        !m_assignKeys.initialize(device, m_sortCapacity, m_activeCapacity, m_radixSort.getKeysEven(), m_radixSort.getValuesEven(), m_dispatchState) ||
        !m_tileBoundaries.initialize(device, m_gridX * m_gridY, m_sortCapacity, m_dispatchState) ||
        !m_render.initialize(device, width, height, m_dispatchState) ||
        (m_fusionEnabled && !CreateFusionPass()) || !CreatePreparePass()) return false;
    for (auto& slot : m_telemetry) {
        slot.buffer = device->createBuffer(TemporaryDesc(ESplatDispatchState::Bytes, RHI::EResourceUsage::TransferDst, RHI::EMemoryUsage::Readback));
        if (!slot.buffer) return false;
    }
    if (!MeasureTemporaryBuffers(m_stats.temporaryBytes)) return false;
    RHI::FBufferAllocationInfo image{};
    if (!m_render.getOutputTexture()->getAllocationInfo(image)) return false;
    m_stats.outputImageBytes = image.backingBytes;
    m_stats.sourceCapacity = m_sourceCapacity; m_stats.activeCapacity = m_activeCapacity; m_stats.sortCapacity = m_sortCapacity;
    m_stats.timingAvailable = device->supportsGPUTimestamps(); m_render.SetGPUDispatchState(m_dispatchState);
    const auto& out = m_preprocess.getOutput();
    // Static bindings are baked before submission; scan result descriptors use
    // completion-protected slots when the number of scan steps changes.
    for (uint32 slot = 0; slot < 2; ++slot) {
        m_tileBoundaries.SelectFrameSlot(slot); m_tileBoundaries.setSortedKeys(m_radixSort.getSortedKeys());
        m_render.SelectFrameSlot(slot); m_render.setTileRanges(m_tileBoundaries.getTileRanges());
        m_render.setSortedIds(m_radixSort.getSortedValues()); m_render.setColors(out.rgb);
        m_render.setConicOpacity(out.conicOpacity); m_render.setPointsXY(out.pointsXY);
    }
    setRenderSettings(m_renderSettings); m_bInitialized = true; return true;
}
bool FSplatPipeline::CreatePreparePass() {
    using namespace RHI;
    FDescriptorSetLayoutDesc sets(0, "SplatPrepareDispatch");
    sets.bindings.Add(FDescriptorSetLayoutBinding(0, EDescriptorType::StorageBuffer, EShaderStage::Compute));
    sets.bindings.Add(FDescriptorSetLayoutBinding(1, EDescriptorType::StorageBuffer, EShaderStage::Compute));
    if(m_splitFusionEnabled)sets.bindings.Add(FDescriptorSetLayoutBinding(2, EDescriptorType::StorageBuffer, EShaderStage::Compute));
    m_prepareSetLayout = m_device->createDescriptorSetLayout(sets); if (!m_prepareSetLayout) return false;
    FPipelineLayoutDesc layout("SplatPrepareDispatch"); layout.setLayouts.Add(m_prepareSetLayout);
    layout.pushConstantRanges.Add(FPushConstantRange(EShaderStage::Compute, 0, 16));
    m_prepareLayout = m_device->createPipelineLayout(layout); if (!m_prepareLayout) return false;
    const auto bytes = ShaderCompiler::readFileBytes(SplatShaderPath(m_splitFusionEnabled?
        "Shaders/Splat/Sort/compiled/splat_prepare_split_dispatch.spv":"Shaders/Splat/Sort/compiled/splat_prepare_dispatch.spv"));
    if (bytes.empty()) return false;
    auto shader = m_device->createComputeShader(TSpan<const uint8>(bytes.data(), bytes.size())); if (!shader) return false;
    ComputePipelineStateDesc pipeline;
    pipeline.computeShader = shader; pipeline.pipelineLayout = m_prepareLayout; pipeline.debugName = "SplatPrepareDispatch";
    m_preparePipeline = m_device->createComputePipelineState(pipeline); if (!m_preparePipeline) return false;
    for (auto& set : m_prepareSets) {
        set = m_device->allocateDescriptorSet(m_prepareSetLayout); if (!set) return false;
        set->updateStorageBuffer(1, m_dispatchState, 0, 0);
        if(m_splitFusionEnabled)set->updateStorageBuffer(2,m_splitFusion.GetCounter(),0,FSplatSplitFusionPass::CounterBytes);
    }
    return true;
}
bool FSplatPipeline::CreateFusionPass() {
    if(m_splitFusionEnabled)return m_splitFusion.Initialize(m_device,m_activeCapacity,m_preprocess.getOutput());
    using namespace RHI;
    static_assert(sizeof(FSplatFusionGroup) == 8);
    FDescriptorSetLayoutDesc setDesc(0, "SplatProjectedFusion");
    for (uint32 binding = 0; binding < 10; ++binding)
        setDesc.bindings.Add(FDescriptorSetLayoutBinding(binding, EDescriptorType::StorageBuffer, EShaderStage::Compute));
    m_fusionSetLayout = m_device->createDescriptorSetLayout(setDesc); if (!m_fusionSetLayout) return false;
    FPipelineLayoutDesc layout("SplatProjectedFusion"); layout.setLayouts.Add(m_fusionSetLayout);
    layout.pushConstantRanges.Add(FPushConstantRange(EShaderStage::Compute, 0, 32));
    m_fusionLayout = m_device->createPipelineLayout(layout); if (!m_fusionLayout) return false;
    const auto bytes = ShaderCompiler::readFileBytes(SplatShaderPath("Shaders/Splat/compiled/splat_fusion.spv"));
    if (bytes.empty()) return false;
    auto shader = m_device->createComputeShader(TSpan<const uint8>(bytes.data(), bytes.size())); if (!shader) return false;
    ComputePipelineStateDesc pipeline;
    pipeline.computeShader = shader; pipeline.pipelineLayout = m_fusionLayout; pipeline.debugName = "SplatProjectedFusion";
    m_fusionPipeline = m_device->createComputePipelineState(pipeline); if (!m_fusionPipeline) return false;
    m_fusionCounter = m_device->createBuffer(TemporaryDesc(16, StorageUsage)); if (!m_fusionCounter) return false;
    m_fusionCache = m_device->createBuffer(TemporaryDesc(uint64_t(m_activeCapacity) * 16, StorageUsage)); if (!m_fusionCache) return false;
    const auto& out = m_preprocess.getOutput();
    const MonsterEngine::TSharedPtr<IRHIBuffer> outputs[] = {out.radii, out.depth, out.rgb,
        out.conicOpacity, out.pointsXY, out.tilesTouched, out.bbox};
    for (uint32 slot = 0; slot < 2; ++slot) {
        m_fusionGroupsBuffers[slot] = m_device->createBuffer(TemporaryDesc(uint64_t(m_activeCapacity) * sizeof(FSplatFusionGroup) + 16,
            EResourceUsage::StorageBuffer | EResourceUsage::TransferSrc, EMemoryUsage::Dynamic));
        m_fusionSets[slot] = m_device->allocateDescriptorSet(m_fusionSetLayout);
        if (!m_fusionGroupsBuffers[slot] || !m_fusionSets[slot]) return false;
        for (uint32 binding = 0; binding < 7; ++binding) m_fusionSets[slot]->updateStorageBuffer(binding, outputs[binding], 0, 0);
        m_fusionSets[slot]->updateStorageBuffer(7, m_fusionGroupsBuffers[slot], 0, uint64_t(m_activeCapacity) * sizeof(FSplatFusionGroup));
        m_fusionSets[slot]->updateStorageBuffer(8, m_fusionCounter, 0, 16);
        m_fusionSets[slot]->updateStorageBuffer(9, m_fusionCache, 0, 0);
    }
    return true;
}
bool FSplatPipeline::ExecuteFusion(RHI::IRHICommandList* cmd, uint32 slot, uint32 count) {
    if(m_splitFusionEnabled)return m_splitFusion.Execute(cmd,slot,count,m_gridX,m_gridY,m_frame,m_renderSettings.fusionRelativeRGBAError);
    if (m_fusionGroups.empty()) return true;
    // This frame slot has already passed the actual submission-completion gate.
    void* mapped = m_fusionGroupsBuffers[slot]->map(); if (!mapped) return false;
    std::memcpy(mapped, m_fusionGroups.data(), m_fusionGroups.size() * sizeof(FSplatFusionGroup));
    const uint32 zeroOffset = m_activeCapacity * sizeof(FSplatFusionGroup);
    const uint32 counters[4]={0,0,1,0};std::memcpy(static_cast<uint8*>(mapped)+zeroOffset,counters,sizeof(counters));m_fusionGroupsBuffers[slot]->unmap();
    cmd->copyBuffer(m_fusionCounter, m_fusionGroupsBuffers[slot], 16, 0, zeroOffset); cmd->resourceBarrier();
    struct alignas(16) Push {uint32 groups,count,x,y;float error;uint32 cacheCapacity,phase,frame;};
    Push push{uint32(m_fusionGroups.size()), count, m_gridX, m_gridY,m_renderSettings.fusionRelativeRGBAError,m_activeCapacity,0,uint32(m_frame)};
    cmd->setPipelineState(m_fusionPipeline); cmd->bindDescriptorSet(m_fusionLayout, 0, m_fusionSets[slot]);
    if (!m_fusionCacheCleared) {
        push.phase=1;
        cmd->pushConstants(m_fusionLayout, RHI::EShaderStage::Compute, 0, sizeof(push), &push);
        const uint32 clearGroups=(m_activeCapacity+31u)/32u;
        cmd->dispatch(std::min(clearGroups,65535u),(clearGroups+65534u)/65535u,1);cmd->resourceBarrier();
        m_pendingFusionCacheClear=true;push.phase=0;
    }
    cmd->pushConstants(m_fusionLayout, RHI::EShaderStage::Compute, 0, sizeof(push), &push);
    const uint32 groups = (push.groups + 31u) / 32u;
    cmd->dispatch(std::min(groups, 65535u), (groups + 65534u) / 65535u, 1); cmd->resourceBarrier();
    return true;
}
void FSplatPipeline::setGaussianData(const FSplatGPUData& data) {
    if(m_splitFusionEnabled&&(data.positions!=m_fusionSourceIdentity.positions||data.scales!=m_fusionSourceIdentity.scales||
        data.rotations!=m_fusionSourceIdentity.rotations||data.opacities!=m_fusionSourceIdentity.opacities||
        data.shCoefficients!=m_fusionSourceIdentity.shCoefficients||data.shDegree!=m_fusionSourceIdentity.shDegree))m_splitFusion.InvalidateCheckpoint();
    m_fusionSourceIdentity=data;
    m_hasGaussianData = data.isValid() && data.gaussianCount <= m_sourceCapacity && data.shDegree >= 0 && data.shDegree <= 3;
    if (!m_hasGaussianData) return;
    const uint64_t n = data.gaussianCount, sh = uint64_t(data.shDegree + 1) * (data.shDegree + 1);
    m_hasGaussianData = data.positions->getSize() >= n * 16 && data.scales->getSize() >= n * 16 &&
        data.rotations->getSize() >= n * 16 && data.opacities->getSize() >= n * 4 && data.shCoefficients->getSize() >= n * sh * 12;
    if (m_hasGaussianData) { m_preprocess.setInputs(data); m_gaussianCount = data.gaussianCount; }
}
void FSplatPipeline::setCamera(const FCameraUniforms& camera) {
    if(m_splitFusionEnabled&&(camera.shDegree!=m_fusionCameraIdentity.shDegree||
        std::memcmp(camera.projMatrix,m_fusionCameraIdentity.projMatrix,sizeof(camera.projMatrix))))m_splitFusion.InvalidateCheckpoint();
    m_fusionCameraIdentity=camera;
    m_hasCamera = camera.imageWidth == int32(m_imageWidth) && camera.imageHeight == int32(m_imageHeight) && camera.shDegree >= 0 && camera.shDegree <= 3;
    for (float v : camera.viewMatrix) m_hasCamera = m_hasCamera && std::isfinite(v);
    for (float v : camera.projMatrix) m_hasCamera = m_hasCamera && std::isfinite(v);
    for (float v : camera.camPos) m_hasCamera = m_hasCamera && std::isfinite(v);
    m_hasCamera = m_hasCamera && std::isfinite(camera.focalX) && std::isfinite(camera.focalY) && std::isfinite(camera.tanFovX) &&
        std::isfinite(camera.tanFovY) && camera.focalX > 0 && camera.focalY > 0;
    if (m_hasCamera) m_preprocess.updateCamera(camera);
}
void FSplatPipeline::SetActiveSet(const FSplatActiveSet& set) {
    if(m_splitFusionEnabled&&set.sceneGeneration!=m_activeSet.sceneGeneration)m_splitFusion.InvalidateCheckpoint();
    m_activeSet.sourceIndices = set.sourceIndices; m_activeSet.selectionVersion = set.selectionVersion;
    m_activeSet.residencyVersion = set.residencyVersion; m_hasActiveSet = true;
    m_activeSet.sceneGeneration=set.sceneGeneration;
    if (!SetFusionGroups(set.fusionGroups)) { m_fusionGroups.clear(); m_hasGaussianData = false; }
}
void FSplatPipeline::UseFullGaussianSet() { m_activeSet = {}; m_hasActiveSet = false; m_fusionGroups.clear();if(m_splitFusionEnabled){m_splitFusion.SetGroups({});m_splitFusion.InvalidateCheckpoint();} }
uint64_t FSplatPipeline::ReserveFrameId() {
    m_frameReserved = true; m_stats.frame = ++m_frame; return m_frame;
}
void FSplatPipeline::setRenderSettings(const FSplatRenderSettings& settings) {
    FSplatRenderSettings safe = settings;
    if (!std::isfinite(safe.nearPlane) || safe.nearPlane <= 0 || safe.nearPlane > 1e6f) safe.nearPlane = 0.1f;
    if (!std::isfinite(safe.farPlane) || safe.farPlane <= safe.nearPlane || safe.farPlane > 1e9f) safe.farPlane = std::max(1000.0f, safe.nearPlane + 1);
    if (!std::isfinite(safe.gaussianScale) || safe.gaussianScale <= 0) safe.gaussianScale = 1;
    safe.gaussianScale = std::clamp(safe.gaussianScale, 0.01f, 10.0f);
    if(!std::isfinite(safe.fusionRelativeRGBAError)||safe.fusionRelativeRGBAError<=0||safe.fusionRelativeRGBAError>1) safe.fusionRelativeRGBAError=0.4f;
    if(m_splitFusionEnabled&&(safe.gaussianScale!=m_renderSettings.gaussianScale||safe.nearPlane!=m_renderSettings.nearPlane||
        safe.farPlane!=m_renderSettings.farPlane||safe.distanceCulling!=m_renderSettings.distanceCulling||
        safe.fusionRelativeRGBAError!=m_renderSettings.fusionRelativeRGBAError))m_splitFusion.InvalidateCheckpoint();
    m_renderSettings = safe;
    m_preprocess.setClipPlanes(safe.nearPlane, safe.farPlane); m_preprocess.setDistanceCulling(safe.distanceCulling);
    m_preprocess.setGaussianScale(safe.gaussianScale); m_render.setWireframe(safe.wireframe);
}
void FSplatPipeline::PollTelemetry() {
    m_stats.countSamples.clear();
    m_stats.timestampSamples = m_device->pollGPUTimestamps();
    for (auto& slot : m_telemetry) {
        if (!slot.pending || !slot.token.value || !m_device->isSubmissionComplete(slot.token)) continue;
        void* mapped = slot.buffer->map(); if (!mapped) continue;
        uint32 state[32]; std::memcpy(state, mapped, sizeof(state)); slot.buffer->unmap();
        float fusionError=0;std::memcpy(&fusionError,&state[23],sizeof(fusionError));
        m_stats.countSamples.push_back({slot.frame,state[0],state[1]!=0,state[20],state[21],state[2],state[22]!=0,fusionError});
        auto& sample=m_stats.countSamples.back();sample.fusionFreshQueued=state[24];sample.fusionExactQueued=state[26];
        sample.fusionCertificatePass=state[28];sample.fusionCertificateFail=state[29];sample.fusionExactChecks=state[30];
        sample.fusionOriginalVisible=state[31];sample.fusionQueueOverflow=state[25]!=0||state[27]!=0;sample.fusionUsesCertificates=m_splitFusionEnabled;
        if (state[1]) ++m_stats.overflowFrames;
        if (slot.frame >= m_stats.countFrame) {
            m_stats.countFrame = slot.frame; m_stats.actualSortCount = state[0]; m_stats.overflow = state[1] != 0; m_stats.countAvailable = true;
            m_stats.renderedPrimitives = state[20]; m_stats.fusedPrimitives = state[21];m_stats.fusionApplied=state[22]!=0;
            m_stats.maxFusionRelativeRGBAError=fusionError;
        }
        slot.pending = false; slot.token = {};
    }
}
const FSplatPipelineFrameStats& FSplatPipeline::CollectCompletedFrameStats() {
    if(m_device)PollTelemetry();
    return m_stats;
}
void FSplatPipeline::MarkFrameSubmitted(RHI::FGPUCompletionToken token) {
    if (m_recordedFrameSlot < 0 || !token.value || token.value == m_beforeSubmission.value ||
        token.value != m_device->getLastSubmissionToken().value) return;
    m_frameTokens[m_recordedFrameSlot] = token;
    if (m_pendingTelemetry >= 0) m_telemetry[m_pendingTelemetry].token = token;
    if (m_pendingOutputClear) m_outputCleared = true;
    if (m_pendingFusionCacheClear) m_fusionCacheCleared = true;
    if(m_splitFusionEnabled)m_splitFusion.ConfirmSubmitted();
    m_pendingOutputClear = false;
    m_pendingFusionCacheClear = false;
    m_recordedFrameSlot = -1; m_pendingTelemetry = -1;
}
MonsterEngine::TSharedPtr<RHI::IRHITexture> FSplatPipeline::execute(RHI::IRHICommandList* cmd) {
    using namespace RHI;
    if (!m_bInitialized || !cmd) return nullptr;
    // Legacy caller compatibility: only a changed real submission can associate
    // the preceding recording. Explicit callers mark immediately after present.
    MarkFrameSubmitted(m_device->getLastSubmissionToken());
    if (m_recordedFrameSlot >= 0) {
        if (m_pendingTelemetry >= 0) m_telemetry[m_pendingTelemetry].pending = false;
        m_pendingOutputClear = false;
        m_pendingFusionCacheClear = false;
        if(m_splitFusionEnabled)m_splitFusion.DiscardRecording();
        m_recordedFrameSlot = -1; m_pendingTelemetry = -1;
    }
    PollTelemetry();
    if (!m_frameReserved) ReserveFrameId();
    m_frameReserved = false;
    const uint64_t activeSize = m_hasActiveSet ? m_activeSet.sourceIndices.size() : m_gaussianCount;
    m_stats.degraded = false; m_stats.degradationReason.clear();
    auto fail = [&](const char* reason) {
        m_stats.degraded = true; m_stats.degradationReason = reason;
        return (m_outputCleared || m_pendingOutputClear) ? m_render.getOutputTexture() : MonsterEngine::TSharedPtr<IRHITexture>{};
    };
    if (!m_hasCamera || (activeSize && !m_hasGaussianData)) return fail("Missing valid input data or camera");
    if (activeSize > m_activeCapacity) return fail("Active count exceeds compact capacity");
    if (!m_fusionGroups.empty() && uint64_t(m_fusionGroups.back().First()) + m_fusionGroups.back().Count() != activeSize)
        return fail("Fusion groups do not exactly partition the current original active mapping");
    if (!m_preprocess.setActiveIndices(m_activeSet.sourceIndices, m_hasActiveSet)) return fail("Invalid source index mapping");
    int frameSlot = -1;
    for (uint32 slot = 0; slot < 2; ++slot)
        if (!m_frameTokens[slot].value || m_device->isSubmissionComplete(m_frameTokens[slot])) { frameSlot = int(slot); break; }
    if (frameSlot < 0) return fail("Mapped input slots still referenced by GPU submissions");
    m_recordedFrameSlot = frameSlot; m_beforeSubmission = m_device->getLastSubmissionToken();
    m_stats.activeCount = uint32(activeSize);
    m_stats.selectionVersion = m_activeSet.selectionVersion; m_stats.residencyVersion = m_activeSet.residencyVersion;
    if (m_stats.overflow) { m_stats.degraded = true; m_stats.degradationReason = "Sort overflow: prior output retained; tighten selection budget"; }
    const uint32 count = uint32(activeSize);
    m_preprocess.SelectFrameSlot(frameSlot); m_assignKeys.SelectFrameSlot(frameSlot);
    m_tileBoundaries.SelectFrameSlot(frameSlot); m_render.SelectFrameSlot(frameSlot);
    const auto& out = m_preprocess.getOutput(); cmd->resourceBarrier();
    cmd->writeGPUTimestamp(m_frame, "Pipeline", true);
    if (!m_outputCleared) {
        cmd->transitionResource(m_render.getOutputTexture(), EResourceUsage::None, EResourceUsage::UnorderedAccess);
        m_render.execute(cmd, true); cmd->resourceBarrier(); m_render.SelectFrameSlot(frameSlot); m_pendingOutputClear = true;
    }
    cmd->writeGPUTimestamp(m_frame, "Preprocess", true);
    if (count && !m_preprocess.execute(cmd, count)) {
        cmd->writeGPUTimestamp(m_frame, "Preprocess", false); cmd->writeGPUTimestamp(m_frame, "Pipeline", false);
        return fail("Mapped input upload failed");
    }
    cmd->resourceBarrier();
    if ((count||m_splitFusionEnabled) && !ExecuteFusion(cmd, uint32(frameSlot), count)) {
        cmd->writeGPUTimestamp(m_frame, "Preprocess", false); cmd->writeGPUTimestamp(m_frame, "Pipeline", false);
        return fail("Fusion descriptor upload failed");
    }
    cmd->writeGPUTimestamp(m_frame, "Preprocess", false);
    cmd->writeGPUTimestamp(m_frame, "Scan", true);
    if (count) { cmd->copyBuffer(m_prefixSum.getBufferA(), out.tilesTouched, uint64_t(count) * 4); cmd->resourceBarrier(); m_prefixSum.execute(cmd, count); }
    cmd->resourceBarrier();
    auto& set = m_prepareSets[frameSlot]; set->updateStorageBuffer(0, m_prefixSum.getResultBuffer(), 0, 0);
    const uint32 prepare[4] = {count, m_sortCapacity, m_gridX, m_gridY};
    cmd->setPipelineState(m_preparePipeline); cmd->bindDescriptorSet(m_prepareLayout, 0, set);
    cmd->pushConstants(m_prepareLayout, EShaderStage::Compute, 0, sizeof(prepare), prepare); cmd->dispatch(1, 1, 1); cmd->resourceBarrier();
    if(m_splitFusionEnabled){cmd->copyBuffer(m_dispatchState,m_splitFusion.GetCounter(),FSplatSplitFusionPass::CounterBytes,80,0);cmd->resourceBarrier();}
    else if (!m_fusionGroups.empty()) { cmd->copyBuffer(m_dispatchState, m_fusionCounter, 16, 80, 0); cmd->resourceBarrier(); }
    cmd->writeGPUTimestamp(m_frame, "Scan", false);
    cmd->writeGPUTimestamp(m_frame, "AssignKeys", true);
    m_assignKeys.setInputBuffers(m_prefixSum.getResultBuffer(), out.bbox, out.depth); m_assignKeys.execute(cmd, count, m_gridX, m_gridY);
    cmd->resourceBarrier(); cmd->writeGPUTimestamp(m_frame, "AssignKeys", false);
    cmd->writeGPUTimestamp(m_frame, "RadixSort", true); m_radixSort.execute(cmd, 0, uint64_t(m_gridX)*m_gridY<=65536 ? 48 : 64); cmd->resourceBarrier(); cmd->writeGPUTimestamp(m_frame, "RadixSort", false);
    cmd->writeGPUTimestamp(m_frame, "TileBounds", true); m_tileBoundaries.execute(cmd, 0); cmd->resourceBarrier(); cmd->writeGPUTimestamp(m_frame, "TileBounds", false);
    cmd->writeGPUTimestamp(m_frame, "Render", true); m_render.execute(cmd); cmd->resourceBarrier(); cmd->writeGPUTimestamp(m_frame, "Render", false);
    cmd->writeGPUTimestamp(m_frame, "Pipeline", false);
    for (uint32 slot = 0; slot < 3; ++slot) if (!m_telemetry[slot].pending) {
        cmd->copyBuffer(m_telemetry[slot].buffer, m_dispatchState, ESplatDispatchState::Bytes); cmd->resourceBarrier();
        m_telemetry[slot].pending = true; m_telemetry[slot].token = {}; m_telemetry[slot].frame = m_frame; m_pendingTelemetry = int(slot); break;
    }
    if (m_pendingTelemetry < 0) ++m_stats.telemetrySkippedFrames;
    return m_render.getOutputTexture();
}
} // namespace MonsterRender::Splat
