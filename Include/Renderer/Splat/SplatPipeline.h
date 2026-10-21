// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include "Renderer/Splat/SplatPass.h"
#include "Renderer/Splat/SplatSortPass.h"
#include "Renderer/Splat/SplatRenderPass.h"
#include "Renderer/Splat/SplatSplitFusionPass.h"
#include "Renderer/Splat/LOD/SplatLODTypes.h"
#include "RHI/SplatGPUPrimitives.h"
#include <vector>
#include <string>
#include <array>
#include <map>

namespace MonsterRender::Splat {
/** One actual completed asynchronous count readback. */
struct FSplatGPUCountSample {
    uint64_t frame = 0; ///< Original recording frame ID.
    uint32_t count = 0; ///< GPU tile-entry count; saturates on overflow.
    bool overflow = false; ///< GPU capacity overflow flag.
    uint32_t renderedPrimitives = 0, fusedPrimitives = 0; ///< Actual same-frame projected outputs.
    uint32_t originalActiveCount = 0; ///< Dispatch source extent of the measured frame.
    bool fusionApplied = false; ///< False means renderedPrimitives is unavailable, not zero.
    float maxFusionRelativeRGBAError = 0; ///< Exact error or conservative certificate bound for admitted groups.
    uint32_t fusionFreshQueued=0, fusionExactQueued=0, fusionCertificatePass=0, fusionCertificateFail=0, fusionExactChecks=0, fusionOriginalVisible=0;
    bool fusionQueueOverflow=false, fusionUsesCertificates=false;
};
/** Telemetry is asynchronous; countFrame identifies the submitted frame measured. */
struct FSplatPipelineFrameStats {
    uint64_t frame = 0; ///< Current recorded frame.
    uint64_t countFrame = 0; ///< Completed GPU-count sample frame.
    uint64_t selectionVersion = 0, residencyVersion = 0; ///< Active mapping versions.
    uint32_t sourceCapacity = 0, activeCount = 0, activeCapacity = 0; ///< Input/output counts.
    uint32_t actualSortCount = 0, sortCapacity = 0; ///< Measured count; saturates on overflow.
    uint32_t renderedPrimitives = 0, fusedPrimitives = 0; ///< Delayed actual fusion output, joined by countFrame.
    bool fusionApplied = false; ///< Delayed fusion count availability.
    float maxFusionRelativeRGBAError = 0; ///< Measured maximum group error, joined by countFrame.
    uint64_t temporaryBytes = 0; ///< Reserved pipeline buffers/images/staging.
    uint64_t outputImageBytes = 0; ///< Physical output image bytes included in temporaryBytes.
    uint32 projectionColorStorageMode = 0; ///< Actual allocation and shader mode: FP32=0, RGB21F=1.
    uint64_t projectionColorLogicalBytes = 0, projectionColorReservedBytes = 0, projectionColorBackingBytes = 0; ///< Actual color allocation sizes.
    uint64_t projectionColorRequiredAllocationBytes = 0, projectionColorAllocationAlignmentBytes = 0; ///< Device allocation requirements.
    uint64_t radixKeysBytes = 0, radixValuesBytes = 0, radixHistogramBytes = 0; ///< Physical dedicated radix allocations, included in temporaryBytes.
    uint64_t overflowFrames = 0; ///< Completed samples containing overflow.
    uint64_t telemetrySkippedFrames = 0; ///< Frames without a free asynchronous readback slot.
    bool overflow = false, degraded = false, countAvailable = false; ///< Delayed GPU sample.
    bool countRecordingSkipped = false; ///< Current recording has no free count readback slot.
    bool timingAvailable = false; ///< Hardware timestamp capability.
    std::string degradationReason; ///< Configuration/capacity failure.
    uint32 tightTileCullingRequested = 0, tightTileCullingEffective = 0; ///< Count/emit mode selected for this pipeline.
    std::string tightTileCullingFallback; ///< Explicit compatibility fallback reason, empty if none.
    uint32 radixSortBlocksPerWorkgroupRequested = RADIX_SORT_BLOCKS_PER_WG; ///< Validated requested workload.
    uint32 radixSortBlocksPerWorkgroupEffective = RADIX_SORT_BLOCKS_PER_WG; ///< Allocated and dispatched workload.
    bool radixPhaseTimingEnabled = false; ///< Extra per-round queries; disabled for normal interactive rendering.
    std::vector<RHI::FRHIGPUTimestampSample> timestampSamples; ///< Resolved real GPU intervals.
    std::vector<FSplatGPUCountSample> countSamples; ///< Every resolved count since the preceding poll.
};
/** Six compute stages with same-frame GPU count/args and bounded persistent storage. */
class FSplatPipeline {
public:
    FSplatPipeline() = default;
    ~FSplatPipeline();
    /** Call before initialize; capacities are counts and temporaryBytes is a hard byte cap. */
    void SetCapacityBudgets(uint32 sourceCapacity, uint32 activeCapacity,
        uint32 sortCapacity, uint64_t temporaryBytes);
    /** Optional capability must be requested before initialize; no storage when disabled. */
    bool EnableProjectedFusion(bool split = false);
    /** Allocation-time layout; rejects projected fusion and changes after initialization. */
    bool SetProjectionColorCompression(bool enabled);
    /** Validate before allocation; changing workload requires a new pipeline. */
    bool SetRadixSortBlocksPerWorkgroup(uint32 blocks);
    /** Opt in before initialize; parent and child timing scopes overlap. */
    bool SetRadixPhaseTiming(bool enabled);
    /** Groups must exactly partition [0, originalActiveCount), each with 1..4 members.
     * Caller owns original-source residency and submission pins; empty disables fusion. */
    bool SetFusionGroups(const std::vector<FSplatFusionGroup>& groups);
    /** Compatibility entrypoint; legacy defaults are capped before allocating. */
    bool initialize(RHI::IRHIDevice* device, uint32 gaussianCount, uint32 imageWidth,
        uint32 imageHeight, uint32 maxTilesPerGaussian = 64);
    /** Owner preflight for aggregate admission before creating pipeline backing.
     * Uses the exact same capacity/default sizing and allocation requirements. */
    bool QueryTemporaryBytes(RHI::IRHIDevice*, uint32 count, uint32 width, uint32 height, uint64_t& bytes);
    void setGaussianData(const FSplatGPUData& data);
    void setCamera(const FCameraUniforms& camera);
    void setRenderSettings(const FSplatRenderSettings& settings);
    /** Empty active set produces a valid transparent frame. */
    void SetActiveSet(const FSplatActiveSet& activeSet);
    /** Restore implicit sourceIdx=activeIdx full-PLY mode. */
    void UseFullGaussianSet();
    /** Reserve the original recording ID before application uploads/timestamps. */
    uint64_t ReserveFrameId();
    MonsterEngine::TSharedPtr<RHI::IRHITexture> execute(RHI::IRHICommandList* cmdList);
    /** Associate recorded telemetry with successful real submission, after present. */
    void MarkFrameSubmitted(RHI::FGPUCompletionToken completionToken);
    const FSplatPipelineFrameStats& GetFrameStats() const { return m_stats; }
    /** Poll completed telemetry without recording GPU work or waiting. */
    const FSplatPipelineFrameStats& CollectCompletedFrameStats();
    bool isInitialized() const { return m_bInitialized; }
    MonsterEngine::TSharedPtr<RHI::IRHITexture> getOutputTexture() const { return m_render.getOutputTexture(); }
    uint32 getGaussianCount() const { return m_gaussianCount; }
    uint32 getImageWidth() const { return m_imageWidth; }
    uint32 getImageHeight() const { return m_imageHeight; }
    /** Read only after submitted completion in GPU verification fixtures. */
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> GetDispatchStateBuffer() const { return m_dispatchState; }
    /** Production preprocess output; read only after submitted completion. */
    const FSplatPreprocessOutput& GetPreprocessOutput() const { return m_preprocess.getOutput(); }
private:
    bool CreatePreparePass();
    bool CreateFusionPass();
    bool ExecuteFusion(RHI::IRHICommandList*, uint32_t frameSlot, uint32_t sourceCount);
    bool PreflightTemporaryBuffers(uint64_t& bytes);
    bool MeasureTemporaryBuffers(uint64_t& bytes) const;
    void PollTelemetry();
    struct FTelemetrySlot {
        MonsterEngine::TSharedPtr<RHI::IRHIBuffer> buffer;
        RHI::FGPUCompletionToken token{};
        uint64_t frame = 0;
        bool pending = false;
    };
    FSplatPreprocessPass m_preprocess;
    FSplatPrefixSumPass m_prefixSum;
    FSplatAssignKeysPass m_assignKeys;
    FSplatRadixSortPass m_radixSort;
    FSplatTileBoundariesPass m_tileBoundaries;
    FSplatRenderPass m_render;
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_dispatchState;
    MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSetLayout> m_prepareSetLayout;
    MonsterEngine::TSharedPtr<RHI::IRHIPipelineLayout> m_prepareLayout;
    MonsterEngine::TSharedPtr<RHI::IRHIPipelineState> m_preparePipeline;
    MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSet> m_prepareSets[2];
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_fusionGroupsBuffers[2], m_fusionCounter, m_fusionCache;
    MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSetLayout> m_fusionSetLayout;
    MonsterEngine::TSharedPtr<RHI::IRHIPipelineLayout> m_fusionLayout;
    MonsterEngine::TSharedPtr<RHI::IRHIPipelineState> m_fusionPipeline;
    MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSet> m_fusionSets[2];
    std::vector<FSplatFusionGroup> m_fusionGroups;
    bool m_fusionEnabled = false, m_splitFusionEnabled = false;
    FSplatSplitFusionPass m_splitFusion;
    FSplatGPUData m_fusionSourceIdentity;
    FCameraUniforms m_fusionCameraIdentity{};
    bool m_fusionCacheCleared = false, m_pendingFusionCacheClear = false;
    FTelemetrySlot m_telemetry[3];
    int m_pendingTelemetry = -1;
    int m_recordedFrameSlot = -1;
    RHI::FGPUCompletionToken m_frameTokens[2]{};
    RHI::FGPUCompletionToken m_beforeSubmission{};
    RHI::IRHIDevice* m_device = nullptr;
    uint32 m_gaussianCount = 0, m_sourceCapacity = 0, m_activeCapacity = 0, m_sortCapacity = 0;
    uint32 m_imageWidth = 0, m_imageHeight = 0, m_gridX = 0, m_gridY = 0;
    uint64_t m_temporaryBudget = 1ull << 30, m_frame = 0;
    bool m_explicitBudgets = false, m_bInitialized = false, m_hasGaussianData = false;
    bool m_hasCamera = false, m_hasActiveSet = false, m_outputCleared = false;
    bool m_pendingOutputClear = false;
    bool m_frameReserved = false;
    FSplatActiveSet m_activeSet;
    FSplatRenderSettings m_renderSettings{};
    FSplatPipelineFrameStats m_stats;
    struct FRadixTimingFrame {
        std::array<double, 3> milliseconds{}; ///< Sums of complete phase rounds.
        std::array<uint32, 3> masks{}; ///< Observed unique pass bits; missing queries remain unavailable.
    };
    std::map<uint64, FRadixTimingFrame> m_radixTimings;
};
} // namespace MonsterRender::Splat
