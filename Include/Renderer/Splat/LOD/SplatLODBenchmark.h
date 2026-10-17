// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include "Renderer/Splat/Streaming/SplatGPUPagePool.h"
#include "Renderer/Splat/Streaming/SplatPageStreamer.h"
#include "Renderer/Splat/SplatPipeline.h"
#include <map>
#include <memory>
namespace MonsterRender::Splat {
/** Registered runtime and benchmark options; capacities are independent counts. */
struct FSplatLODRuntimeOptions {
    std::string scenePath, sourcePLY, sequencePath; ///< Manifest, baseline and camera paths.
    std::string outputDirectory = "TestFile/3DGSLODStreaming/application/output"; ///< Evidence directory.
    std::string cacheCondition = "unknown"; ///< Truthful file-cache condition.
    char mode = 'D'; ///< A original, B full leaf, C resident LOD, D/E streaming.
    bool benchmark = false, captureOnly = false, validation = false, fullLeaf = false; ///< Run policy.
    bool vsync = true; ///< Interactive presentation policy; benchmarks always disable VSync.
    bool functionalSmoke = false; ///< Short functional evidence; captures may show resident fallback while loading. Not formal performance acceptance.
    bool projectedFusion = false; ///< Experimental explicit C/D/E original-source GPU fusion; A/B/fullLeaf always bypass.
    std::string fusionImplementation = "split"; ///< Explicit split or legacy scalar experiment; ignored when fusion is bypassed.
    float fusionRelativeRGBAError = 0.4f; ///< Continuous kernel admission tolerance, never an image-gate override.
    uint32_t width = 1920, height = 1080, rounds = 3; ///< Window/presentation dimensions and rounds.
    uint32_t renderWidth = 0, renderHeight = 0; ///< Optional internal 3DGS dimensions; zero uses the window policy.
    uint32_t activeCapacity = 1000000, sortCapacity = 8000000; ///< Explicit pipeline capacities.
    uint32_t tightTileCulling = 0; ///< 0 baseline, 1 tight rectangle, 2 conservative tile culling.
    uint32_t shStorageMode = 0; ///< 0=FP32, 1=packed FP16; full-resident A only.
    uint32_t rasterSharedMemoryMode = 0; ///< 0 original Raster, 1 FP32 shared-memory batch64.
    uint32 radixSortBlocksPerWorkgroup = RADIX_SORT_BLOCKS_PER_WG; ///< Exact radix workload: 4, 8, 16 or 32.
    bool radixSortPhaseTiming = true; ///< Extra GPU child queries in performance runs; controls overhead experiments.
    double benchmarkFixedCameraSeconds = -1; ///< -1 moving sequence, otherwise fixed sequence sample.
    bool benchmarkFreezeSelection = false; ///< Freeze the target only after full residency convergence.
    int32_t displaySHDegree = -1; ///< -1 means source degree; never changes source stride.
    double warmupSeconds = 10, sampleSeconds = 60, captureSettleSeconds = 2; ///< Fixed wall-clock protocol.
    std::vector<double> captureTimes = {0,15,30,45,60}; ///< Exact camera sampling times.
    FSplatStreamingConfig streaming; ///< Independent byte budgets.
    FSplatLODSettings lod; ///< Quality/hysteresis and selection policy.
    /** Parse a bounded flat JSON config with strict known fields. */
    static bool LoadConfig(const std::string&, FSplatLODRuntimeOptions&, std::string& error);
    /** Reject invalid modes, times, capacities and output paths. */
    bool Validate(std::string& error) const;
};
/** Frame evidence joins delayed GPU intervals/counts by their original frame ID. */
struct FSplatLODBenchmarkFrame {
    uint64_t frame = 0; ///< Pipeline recording ID, independent of swapchain slots.
    double elapsedSeconds = 0, sequenceSeconds = 0, frameMilliseconds = 0, cpuMilliseconds = 0, selectionMilliseconds = 0; ///< CPU clocks.
    uint32_t round = 0; ///< Zero-based registered round.
    bool sampled = false, excluded = false, loading = false, degraded = false; ///< Sample eligibility.
    bool wholeFrameTimingRequired = false; ///< App frames require all application scopes; standalone compute fixtures do not.
    uint64_t selectedLeafNodes = 0, selectedProxyNodes = 0, proxyGaussianCount = 0; ///< Actual submitted cut composition.
    double maxSelectedProxyErrorPixels = 0; ///< Projected combined proxy error; -1 is forced near-plane fallback.
    uint64_t allocatorBytes = 0, allocatorAvailableBytes = 0; ///< Engine live allocations and retained pooled slack, not driver free VRAM.
    uint64_t allocatorPooledReservedBytes = 0, allocatorPooledUsedBytes = 0, allocatorDedicatedCommittedBytes = 0; ///< Actual engine backing breakdown.
    FSplatPipelineFrameStats pipeline; ///< Current recording and delayed samples.
    FSplatStreamingStats io; ///< I/O lifecycle and charged CPU budget.
    FSplatPageStreamerMemoryStats cpu; ///< CPU charge breakdown.
    FSplatGPUPagePoolStats pool; ///< Logical residency and live staging.
    FSplatGPUPagePoolAllocationStats allocations; ///< Measured source/staging backing.
};
/** Bounded incremental recorder and explicit lossless capture-only readback. */
class FSplatLODBenchmark {
public:
    FSplatLODBenchmark();
    ~FSplatLODBenchmark();
    bool Begin(const FSplatLODRuntimeOptions&, const std::map<std::string,std::string>& metadata, std::string& error);
    bool Record(const FSplatLODBenchmarkFrame&, std::string& error);
    /** Resolve final completed intervals after explicit shutdown completion. */
    bool Resolve(const FSplatPipelineFrameStats&, std::string& error);
    /** Retain startup/convergence observations for final metadata without file reads. */
    void SetMetadata(const std::string& key, const std::string& value);
    /** Records raw RGBA8 image copy before output leaves UnorderedAccess. */
    bool RecordCapture(RHI::IRHIDevice*, RHI::IRHICommandList*, MonsterEngine::TSharedPtr<RHI::IRHITexture>,
        uint64_t frame, double sequenceTime, const FCameraUniforms&, uint64_t pipelineTemporaryBytes, std::string& error);
    /** Explicit capture-only wait, write lossless RGBA/PPM and exact camera metadata. */
    bool FinishCapture(RHI::FGPUCompletionToken, std::string& error);
    uint64_t GetCaptureAllocatedBytes() const;
    /** Close files and persist honest completed/failed/unverified status. */
    bool Finish(bool completed, const std::string& reason, std::string& error);
private:
    struct FImpl;
    std::unique_ptr<FImpl> Impl;
};
}
