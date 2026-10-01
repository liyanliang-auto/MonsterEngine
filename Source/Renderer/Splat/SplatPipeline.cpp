/**
 * @file SplatPipeline.cpp
 * @brief Implementation of the 3DGS full-pipeline orchestrator.
 * 
 * Chains: Preprocess → PrefixSum → AssignKeys → RadixSort → 
 * TileBoundaries → Render, with GPU buffer copies and staging readback
 * between passes.
 */

#include "Renderer/Splat/SplatPipeline.h"
#include "Core/Logging/LogMacros.h"
#include "RHI/RHIDefinitions.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace MonsterRender::Splat
{

    // ========================================================================
    // Helpers
    // ========================================================================

    namespace
    {
        /** Create a tiny host-readable staging buffer for GPU readback */
        static MonsterEngine::TSharedPtr<RHI::IRHIBuffer> createStagingBuffer(
            RHI::IRHIDevice* device, uint32 size, const char* name)
        {
            RHI::BufferDesc desc;
            desc.size = size;
            desc.usage = RHI::EResourceUsage::TransferDst;
            desc.memoryUsage = RHI::EMemoryUsage::Readback;
            desc.cpuAccessible = true;
            desc.debugName = name;
            return device->createBuffer(desc);
        }
    }

    // ========================================================================
    // FSplatPipeline
    // ========================================================================

    FSplatPipeline::~FSplatPipeline()
    {
        m_diagDepth.Reset();
        m_stagingBuffer.Reset();
    }

    bool FSplatPipeline::initialize(RHI::IRHIDevice* device, uint32 gaussianCount,
                                     uint32 imageWidth, uint32 imageHeight,
                                     uint32 maxTilesPerGaussian)
    {
        using namespace RHI;

        if (!device || gaussianCount == 0 || imageWidth == 0 || imageHeight == 0)
        {
            MR_LOG(LogTemp, Error, "[SplatPipeline] Invalid initialization parameters");
            return false;
        }

        m_device        = device;
        m_gaussianCount = gaussianCount;
        m_imageWidth    = imageWidth;
        m_imageHeight   = imageHeight;
        m_gridX         = (imageWidth  + 15u) / 16u;
        m_gridY         = (imageHeight + 15u) / 16u;
        m_numTiles      = m_gridX * m_gridY;
        m_maxSortElements = gaussianCount * maxTilesPerGaussian;
        char* diagEnv = nullptr;
        size_t diagEnvLength = 0;
        _dupenv_s(&diagEnv, &diagEnvLength, "MONSTER_SPLAT_DIAG");
        m_enableCulledBoundsDiag = diagEnv != nullptr;
        std::free(diagEnv);

        // Staging buffer for prefix sum readback (8 bytes: exclusiveSum + lastTilesTouched)
        m_stagingBuffer = createStagingBuffer(device, sizeof(uint32) * 2, "SplatPipeline_Staging");
        if (!m_stagingBuffer)
        {
            MR_LOG(LogTemp, Error, "[SplatPipeline] Failed to create staging buffer");
            return false;
        }

        // Preprocess pass
        if (!m_preprocess.initialize(device, gaussianCount))
        {
            MR_LOG(LogTemp, Error, "[SplatPipeline] Preprocess init failed");
            return false;
        }

        // Tile boundaries pass
        if (!m_tileBoundaries.initialize(device, m_numTiles, m_maxSortElements))
        {
            MR_LOG(LogTemp, Error, "[SplatPipeline] TileBoundaries init failed");
            return false;
        }

        // Render pass
        if (!m_render.initialize(device, imageWidth, imageHeight))
        {
            MR_LOG(LogTemp, Error, "[SplatPipeline] Render init failed");
            return false;
        }

        m_bInitialized = true;
        MR_LOG(LogTemp, Log, "[SplatPipeline] Initialized: %u GS, %ux%u, %u tiles (%ux%u), "
               "maxSort=%u", gaussianCount, imageWidth, imageHeight, m_numTiles,
               m_gridX, m_gridY, m_maxSortElements);
        return true;
    }

    bool FSplatPipeline::lazyInitSortPasses(RHI::IRHIDevice* device)
    {
        if (m_sortPassesInitialized)
            return true;

        MR_LOG(LogTemp, Log, "[SplatPipeline] Lazy-init sort passes (maxSort=%u)...",
               m_maxSortElements);

        if (!m_prefixSum.initialize(device, m_gaussianCount))
        {
            MR_LOG(LogTemp, Error, "[SplatPipeline] PrefixSum init failed");
            return false;
        }
        if (!m_assignKeys.initialize(device, m_maxSortElements, m_gaussianCount))
        {
            MR_LOG(LogTemp, Error, "[SplatPipeline] AssignKeys init failed");
            return false;
        }
        if (!m_radixSort.initialize(device, m_maxSortElements))
        {
            MR_LOG(LogTemp, Error, "[SplatPipeline] RadixSort init failed");
            return false;
        }

        m_sortPassesInitialized = true;
        MR_LOG(LogTemp, Log, "[SplatPipeline] Sort passes ready");
        return true;
    }

    bool FSplatPipeline::ensureSortPassesInitialized(RHI::IRHICommandList* cmdList)
    {
        if (m_sortPassesInitialized)
            return true;

        MR_LOG(LogTemp, Log, "[SplatPipeline] First frame: running readback for real sort count...");

        // Initialize sort passes FIRST so their buffers exist before copyBuffer uses them
        if (!lazyInitSortPasses(m_device))
        {
            MR_LOG(LogTemp, Error, "[SplatPipeline] Sort passes init failed before readback");
            return false;
        }

        const FSplatPreprocessOutput& preOut = m_preprocess.getOutput();

        // Run preprocess to compute tilesTouched
        m_preprocess.execute(cmdList, m_gaussianCount);
        cmdList->resourceBarrier();

        // Copy tilesTouched -> prefix sum input
        uint32 copySize = m_gaussianCount * sizeof(uint32);
        cmdList->copyBuffer(m_prefixSum.getBufferA(), preOut.tilesTouched, copySize);
        cmdList->resourceBarrier();

        // Run prefix sum
        m_prefixSum.execute(cmdList, m_gaussianCount);
        cmdList->resourceBarrier();

        // Copy last prefix sum entry + last tilesTouched to staging (8 bytes total)
        cmdList->copyBuffer(m_stagingBuffer, m_prefixSum.getResultBuffer(), sizeof(uint32),
                            0, (m_gaussianCount - 1) * sizeof(uint32));
        cmdList->copyBuffer(m_stagingBuffer, preOut.tilesTouched, sizeof(uint32),
                            sizeof(uint32), (m_gaussianCount - 1) * sizeof(uint32));

        // End recording, SUBMIT to GPU, wait for completion, then read back.
        // NOTE: waitForIdle() only calls vkDeviceWaitIdle and does NOT submit the
        // command buffer, so the readback commands would never execute and the
        // staging buffer would contain garbage -> m_realSortElements would be wrong
        // (typically 0/1), causing the sort+render passes to process ~0 elements
        // and producing a blank window.
        cmdList->end();
        MR_LOG(LogTemp, Log, "[SplatPipeline] Submitting readback command buffer (submitAndWait)...");
        cmdList->submitAndWait();

        void* stagingData = m_stagingBuffer->map();
        if (!stagingData) {
            MR_LOG(LogTemp, Error, "[SplatPipeline] Failed to map staging buffer for readback");
            cmdList->begin();
            return false;
        }
        uint32 exclusiveSum = reinterpret_cast<uint32*>(stagingData)[0];
        uint32 lastTiles = reinterpret_cast<uint32*>(stagingData)[1];
        m_stagingBuffer->unmap();

        m_realSortElements = exclusiveSum + lastTiles;
        if (m_realSortElements == 0) m_realSortElements = 1;
        if (m_realSortElements > m_maxSortElements) m_realSortElements = m_maxSortElements;
        m_assignKeys.setSortElementCount(m_realSortElements);
        m_lastCountCamera = m_currentCamera;
        m_hasCountCamera = true;

        MR_LOG(LogTemp, Log, "[SplatPipeline] Readback: exclusiveSum=%u lastTiles=%u -> totalSortElements=%u (max=%u)",
               exclusiveSum, lastTiles, m_realSortElements, m_maxSortElements);

        // Begin fresh command list for the actual frame rendering
        cmdList->begin();

        return true;
    }

    void FSplatPipeline::setGaussianData(const FSplatGPUData& data)
    {
        m_preprocess.setInputs(data);
        m_hasGaussianData = data.isValid();
    }

    void FSplatPipeline::setCamera(const FCameraUniforms& camera)
    {
        m_preprocess.updateCamera(camera);
        m_preprocess.setClipPlanes(0.1f, 1000.0f);
        m_currentCamera = camera;
        m_prevTanFovY = camera.tanFovY;
        m_hasCamera = true;
    }

    MonsterEngine::TSharedPtr<RHI::IRHITexture> FSplatPipeline::execute(RHI::IRHICommandList* cmdList)
    {
        using namespace RHI;

        if (!m_bInitialized || !cmdList)
        {
            MR_LOG(LogTemp, Error, "[SplatPipeline] Not initialized or null cmdList");
            return nullptr;
        }
        if (!m_hasGaussianData || !m_hasCamera)
        {
            MR_LOG(LogTemp, Error, "[SplatPipeline] Missing input data (gs=%d, cam=%d)",
                   m_hasGaussianData, m_hasCamera);
            return nullptr;
        }
        if (!m_sortPassesInitialized)
        {
            // First frame: run preprocess+prefixSum, read back real totalTiles,
            // initialize sort passes with real count.
            // NOTE: ensureSortPassesInitialized() resets the command buffer
            // (end + begin), which loses any previously recorded commands.
            if (!ensureSortPassesInitialized(cmdList))
            {
                MR_LOG(LogTemp, Error, "[SplatPipeline] Sort passes initialization failed");
                return nullptr;
            }

            // Re-apply output texture layout transition that was lost during
            // the command buffer reset in ensureSortPassesInitialized().
            // The compute shader requires VK_IMAGE_LAYOUT_GENERAL for imageStore.
            auto outputTex = m_render.getOutputTexture();
            if (outputTex)
            {
                cmdList->transitionResource(outputTex,
                    RHI::EResourceUsage::None,
                    RHI::EResourceUsage::UnorderedAccess);
            }
        }

#if 0  // Set to 1 to re-enable per-frame stderr pipeline diagnostics
        fprintf(stderr, "[STDERR] SplatPipeline::execute: BEGIN totalSort=%u gaussians=%u grid=%ux%u tiles=%u\n",
                m_realSortElements, m_gaussianCount, m_gridX, m_gridY, m_numTiles);
#endif

        MR_LOG(LogTemp, Verbose, "[SplatPipeline] === Begin frame === totalSortElements=%u gaussianCount=%u",
               m_realSortElements, m_gaussianCount);

        const FSplatPreprocessOutput& preOut = m_preprocess.getOutput();

        // ---- Pass 1/6: Preprocess ----
#if 0  // Set to 1 to re-enable per-frame stderr pipeline diagnostics
        fprintf(stderr, "[STDERR] SplatPipeline: Pass 1/6 Preprocess start\n");
#endif
        m_preprocess.execute(cmdList, m_gaussianCount);
        cmdList->resourceBarrier();
#if 0  // Set to 1 to re-enable per-frame stderr pipeline diagnostics
        fprintf(stderr, "[STDERR] SplatPipeline: Pass 1/6 Preprocess done\n");
#endif

        // ---- Pass 2/6: Prefix Sum ----
        // Copy tilesTouched from preprocess to prefix sum buffer A
        {
#if 0  // Set to 1 to re-enable per-frame stderr pipeline diagnostics
            fprintf(stderr, "[STDERR] SplatPipeline: Pass 2/6 PrefixSum start\n");
#endif
            uint32 copySize = m_gaussianCount * sizeof(uint32);
            cmdList->copyBuffer(m_prefixSum.getBufferA(), preOut.tilesTouched, copySize);
            cmdList->resourceBarrier();

            // Run Blelloch scan
            m_prefixSum.execute(cmdList, m_gaussianCount);
            cmdList->resourceBarrier();

            MR_LOG(LogTemp, Verbose, "[SplatPipeline] Pass 2/6: PrefixSum done");
#if 0  // Set to 1 to re-enable per-frame stderr pipeline diagnostics
            fprintf(stderr, "[STDERR] SplatPipeline: Pass 2/6 PrefixSum done\n");
#endif
        }

        // The prefix sum above belongs to this camera. Read its count before
        // AssignKeys and RadixSort so neither pass uses the previous view's size.
        if (!refreshSortCountIfNeeded(cmdList))
            return nullptr;
        const uint32 totalSortElements = m_realSortElements;

        // ---- Pass 3/6: Assign Keys ----
#if 0  // Set to 1 to re-enable per-frame stderr pipeline diagnostics
        fprintf(stderr, "[STDERR] SplatPipeline: Pass 3/6 AssignKeys start\n");
#endif
        m_assignKeys.setInputBuffers(
            m_prefixSum.getResultBuffer(),
            preOut.bbox,
            preOut.depth);
        m_assignKeys.execute(cmdList, m_gaussianCount, m_gridX, m_gridY);
        cmdList->resourceBarrier();
#if 0  // Set to 1 to re-enable per-frame stderr pipeline diagnostics
        fprintf(stderr, "[STDERR] SplatPipeline: Pass 3/6 AssignKeys done\n");
#endif

        // ---- Pass 4/6: Radix Sort ----
        // Copy assign keys output → radix sort even buffers
        {
#if 0  // Set to 1 to re-enable per-frame stderr pipeline diagnostics
            fprintf(stderr, "[STDERR] SplatPipeline: Pass 4/6 RadixSort start (elements=%u)\n", totalSortElements);
#endif
            uint32 keySize   = totalSortElements * sizeof(uint64);
            uint32 valueSize = totalSortElements * sizeof(uint32);

            cmdList->copyBuffer(m_radixSort.getKeysEven(),  m_assignKeys.getSortKeys(),   keySize);
            cmdList->copyBuffer(m_radixSort.getValuesEven(), m_assignKeys.getSortValues(), valueSize);
            cmdList->resourceBarrier();

            m_radixSort.execute(cmdList, totalSortElements);
            cmdList->resourceBarrier();

            MR_LOG(LogTemp, Verbose, "[SplatPipeline] Pass 4/6: RadixSort done (%u elements)", totalSortElements);
#if 0  // Set to 1 to re-enable per-frame stderr pipeline diagnostics
            fprintf(stderr, "[STDERR] SplatPipeline: Pass 4/6 RadixSort done\n");
#endif
        }

        // ---- Pass 5/6: Tile Boundaries ----
#if 0  // Set to 1 to re-enable per-frame stderr pipeline diagnostics
        fprintf(stderr, "[STDERR] SplatPipeline: Pass 5/6 TileBoundaries start\n");
#endif
        m_tileBoundaries.setSortedKeys(m_radixSort.getSortedKeys());
        m_tileBoundaries.execute(cmdList, totalSortElements);
        cmdList->resourceBarrier();
#if 0  // Set to 1 to re-enable per-frame stderr pipeline diagnostics
        fprintf(stderr, "[STDERR] SplatPipeline: Pass 5/6 TileBoundaries done\n");
#endif

        // ---- Pass 6/6: Render (EWA alpha blend) ----
#if 0  // Set to 1 to re-enable per-frame stderr pipeline diagnostics
        fprintf(stderr, "[STDERR] SplatPipeline: Pass 6/6 Render start, image=%ux%u\n", m_imageWidth, m_imageHeight);
#endif
        m_render.setTileRanges(m_tileBoundaries.getTileRanges());
        m_render.setSortedIds(m_radixSort.getSortedValues());
        m_render.setColors(preOut.rgb);
        m_render.setConicOpacity(preOut.conicOpacity);
        m_render.setPointsXY(preOut.pointsXY);
        m_render.execute(cmdList);
        if (m_enableCulledBoundsDiag && !m_culledBoundsDiagDone && m_prevTanFovY <= 0.15f)
        {
            m_culledBoundsDiagDone = true;
            diagnoseCulledBounds(cmdList);
        }
#if 0  // Set to 1 to re-enable per-frame stderr pipeline diagnostics
        fprintf(stderr, "[STDERR] SplatPipeline: Pass 6/6 Render done\n");
#endif

        // ---- TEMP DIAGNOSTIC (blank-screen investigation) ----
        // Read back real GPU state once and log it. Does NOT change rendering;
        // it ends+submits+rebegins the command list so onRender can proceed.
        if (!m_diagDone)
        {
            m_diagDone = true;
            diagnoseRenderState(cmdList);
        }

        MR_LOG(LogTemp, Verbose, "[SplatPipeline] === Frame complete ===");
#if 0  // Set to 1 to re-enable per-frame stderr pipeline diagnostics
        fprintf(stderr, "[STDERR] SplatPipeline::execute: END returning output texture\n");
#endif

        return m_render.getOutputTexture();
    }

    // ========================================================================
    // Read the count produced for the current view before generating sort keys.
    // Translation and rotation change tile coverage even when FOV is constant.
    // ========================================================================
    bool FSplatPipeline::refreshSortCountIfNeeded(RHI::IRHICommandList* cmdList)
    {
        const bool changed = !m_hasCountCamera ||
            std::memcmp(m_currentCamera.viewMatrix, m_lastCountCamera.viewMatrix,
                        sizeof(m_currentCamera.viewMatrix)) != 0 ||
            std::memcmp(m_currentCamera.projMatrix, m_lastCountCamera.projMatrix,
                        sizeof(m_currentCamera.projMatrix)) != 0 ||
            m_currentCamera.focalX != m_lastCountCamera.focalX ||
            m_currentCamera.focalY != m_lastCountCamera.focalY ||
            m_currentCamera.tanFovX != m_lastCountCamera.tanFovX ||
            m_currentCamera.tanFovY != m_lastCountCamera.tanFovY ||
            m_currentCamera.imageWidth != m_lastCountCamera.imageWidth ||
            m_currentCamera.imageHeight != m_lastCountCamera.imageHeight;
        const bool fovChanged = !m_hasCountCamera ||
            m_currentCamera.tanFovY != m_lastCountCamera.tanFovY;
        const FSplatPreprocessOutput& preOut = m_preprocess.getOutput();
        static uint32 diagFrame = 0;
        const bool sample = m_enableCulledBoundsDiag && (++diagFrame % 60u == 0u);
        if (!changed && !sample)
            return true;

        // Copy prefix-sum result tail + last tilesTouched into staging.
        cmdList->copyBuffer(m_stagingBuffer, m_prefixSum.getResultBuffer(), sizeof(uint32),
                            0, (m_gaussianCount - 1) * sizeof(uint32));
        cmdList->copyBuffer(m_stagingBuffer, preOut.tilesTouched, sizeof(uint32),
                            sizeof(uint32), (m_gaussianCount - 1) * sizeof(uint32));
        cmdList->resourceBarrier();

        // Submit what we have, wait, then inspect.
        cmdList->end();
        cmdList->submitAndWait();

        void* stagingData = m_stagingBuffer->map();
        if (!stagingData) {
            MR_LOG(LogTemp, Error, "[SplatPipeline] refreshSortCount: staging map failed");
            cmdList->begin();
            return false;
        }
        uint32 exclusiveSum = reinterpret_cast<uint32*>(stagingData)[0];
        uint32 lastTiles    = reinterpret_cast<uint32*>(stagingData)[1];
        m_stagingBuffer->unmap();

        uint32 newCount = exclusiveSum + lastTiles;
        if (newCount == 0) newCount = 1;
        uint32 oldCount = m_realSortElements;
        if (!changed) {
            MR_LOG(LogTemp, Log,
                   "[DIAG][SortCount] pan/sample tanFovY=%.5f used=%u actual=%u delta=%lld",
                   m_prevTanFovY, oldCount, newCount,
                   static_cast<long long>(newCount) - static_cast<long long>(oldCount));
            cmdList->begin();
            auto outputTex = m_render.getOutputTexture();
            if (outputTex) {
                cmdList->transitionResource(outputTex,
                    RHI::EResourceUsage::None, RHI::EResourceUsage::UnorderedAccess);
            }
            return true;
        }
        bool  capped    = false;
        if (newCount > m_maxSortElements) { newCount = m_maxSortElements; capped = true; }
        m_realSortElements = newCount;
        // CRITICAL sync: the AssignKeys shader's bounds guard reads
        // m_assignKeys.m_sortElementCount as its max write index. If we update
        // m_realSortElements but NOT this, the guard would clip at the stale
        // first-frame value while RadixSort processes the new count -> mismatch
        // -> corrupted image. Keep them identical.
        m_assignKeys.setSortElementCount(newCount);
        m_lastCountCamera = m_currentCamera;
        m_hasCountCamera = true;

        MR_LOG(LogTemp, Log,
               "[SplatPipeline] RefreshSortCount: viewChanged=1 fovChanged=%d pos=(%.2f,%.2f,%.2f) tanFovY=%.5f used=%u actual=%u%s (max=%u)",
               fovChanged ? 1 : 0, m_currentCamera.camPos[0], m_currentCamera.camPos[1],
               m_currentCamera.camPos[2], m_prevTanFovY, oldCount, newCount,
               capped ? " [CAPPED]" : "", m_maxSortElements);

        // Re-begin so onRender can keep recording (render output texture needs
        // GENERAL layout for imageStore, same as ensureSortPassesInitialized).
        // CRITICAL: diagnoseRadiusStats() records copyBuffer + resourceBarrier
        // into THIS command list, so it MUST run AFTER begin() — otherwise
        // vkCmdPipelineBarrier hits a non-recording command buffer and the
        // NVIDIA driver crashes (0xC0000005 @ nvoglv64, read 0xFFFFFFFFFFFFFFFF).
        auto outputTex = m_render.getOutputTexture();
        cmdList->begin();
        if (outputTex)
        {
            cmdList->transitionResource(outputTex,
                RHI::EResourceUsage::None,
                RHI::EResourceUsage::UnorderedAccess);
        }

        // Optional readback to correlate large projected splats with depth.
        if (m_enableCulledBoundsDiag && (fovChanged || sample))
        {
            diagnoseRadiusStats(cmdList);
        }
        return true;
    }

    void FSplatPipeline::diagnoseCulledBounds(RHI::IRHICommandList* cmdList)
    {
        auto tiles = createStagingBuffer(m_device, m_gaussianCount * sizeof(uint32), "Diag_TilesTouched");
        auto bounds = createStagingBuffer(m_device, m_gaussianCount * 4u * sizeof(uint32), "Diag_BBox");
        auto ranges = createStagingBuffer(m_device, m_numTiles * 2u * sizeof(uint32), "Diag_ZoomTileRanges");
        if (!tiles || !bounds || !ranges) {
            MR_LOG(LogTemp, Error, "[DIAG][CulledBounds] staging allocation failed");
            return;
        }
        const FSplatPreprocessOutput& out = m_preprocess.getOutput();
        cmdList->copyBuffer(tiles, out.tilesTouched, m_gaussianCount * sizeof(uint32));
        cmdList->copyBuffer(bounds, out.bbox, m_gaussianCount * 4u * sizeof(uint32));
        cmdList->copyBuffer(ranges, m_tileBoundaries.getTileRanges(), m_numTiles * 2u * sizeof(uint32));
        cmdList->resourceBarrier();
        cmdList->end();
        cmdList->submitAndWait();

        auto* tileData = static_cast<uint32*>(tiles->map());
        auto* boundData = static_cast<uint32*>(bounds->map());
        auto* rangeData = static_cast<uint32*>(ranges->map());
        uint32 zeroTilesWithBounds = 0, nonzeroMismatch = 0, zeroTiles = 0;
        uint32 emptyRenderTiles = 0;
        if (!tileData || !boundData || !rangeData) {
            MR_LOG(LogTemp, Error, "[DIAG][CulledBounds] staging map failed");
        }
        if (tileData && boundData) {
            for (uint32 i = 0; i < m_gaussianCount; ++i) {
                const uint32* b = boundData + 4u * i;
                const uint32 area = (b[2] > b[0] && b[3] > b[1])
                    ? (b[2] - b[0]) * (b[3] - b[1]) : 0u;
                if (tileData[i] == 0u) {
                    ++zeroTiles;
                    if (area) ++zeroTilesWithBounds;
                } else if (area != tileData[i]) {
                    ++nonzeroMismatch;
                }
            }
        }
        if (rangeData) {
            for (uint32 i = 0; i < m_numTiles; ++i) {
                if (rangeData[i * 2u] >= rangeData[i * 2u + 1u]) ++emptyRenderTiles;
            }
        }
        if (tileData && boundData && rangeData) {
            MR_LOG(LogTemp, Log,
                   "[DIAG][CulledBounds] tanFovY=%.5f zeroTiles=%u zeroTilesWithBounds=%u nonzeroMismatch=%u emptyRenderTiles=%u/%u",
                   m_prevTanFovY, zeroTiles, zeroTilesWithBounds, nonzeroMismatch, emptyRenderTiles, m_numTiles);
        }
        if (tileData) tiles->unmap();
        if (boundData) bounds->unmap();
        if (rangeData) ranges->unmap();

        cmdList->begin();
        auto outputTex = m_render.getOutputTexture();
        if (outputTex) {
            cmdList->transitionResource(outputTex,
                RHI::EResourceUsage::None, RHI::EResourceUsage::UnorderedAccess);
        }
    }

    // ========================================================================
    // Optional GPU readback of radius/depth distribution.
    // ========================================================================
    void FSplatPipeline::diagnoseRadiusStats(RHI::IRHICommandList* cmdList)
    {
        using namespace RHI;
        if (!m_diagRadii)
            m_diagRadii = createStagingBuffer(m_device, m_gaussianCount * sizeof(uint32), "Diag_Radii");
        if (!m_diagDepth)
            m_diagDepth = createStagingBuffer(m_device, m_gaussianCount * sizeof(float), "Diag_Depth");
        if (!m_diagRadii || !m_diagDepth) {
            MR_LOG(LogTemp, Error, "[DIAG] radii staging allocation failed");
            return;
        }

        const FSplatPreprocessOutput& out = m_preprocess.getOutput();
        cmdList->copyBuffer(m_diagRadii, out.radii, m_gaussianCount * sizeof(uint32));
        cmdList->copyBuffer(m_diagDepth, out.depth, m_gaussianCount * sizeof(float));
        cmdList->resourceBarrier();
        cmdList->end();
        cmdList->submitAndWait();

        int* r = reinterpret_cast<int*>(m_diagRadii->map());
        float* z = reinterpret_cast<float*>(m_diagDepth->map());
        if (!r || !z) {
            MR_LOG(LogTemp, Error, "[DIAG] radii map failed");
            if (r) m_diagRadii->unmap();
            if (z) m_diagDepth->unmap();
            cmdList->begin();
            return;
        }
        uint32 maxR = 0, culled = 0;
        uint64_t sumR = 0;
        uint32 wideByDepth[5] = {}, activeByDepth[5] = {};
        float depthAtMax = 0.0f;
        for (uint32 i = 0; i < m_gaussianCount; ++i) {
            int v = r[i];
            if (v > (int)maxR) { maxR = (uint32)v; depthAtMax = z[i]; }
            if (v <= 0)    culled++;        // fully outside frustum
            sumR += (uint32)(v > 0 ? v : 0);
            if (v > 0) {
                int bucket = z[i] < 0.25f ? 0 : z[i] < 0.5f ? 1 : z[i] < 1.0f ? 2 : z[i] < 2.0f ? 3 : 4;
                ++activeByDepth[bucket];
                if (v >= 30) ++wideByDepth[bucket];
            }
        }
        m_diagRadii->unmap();
        m_diagDepth->unmap();

        float avgR = (float)sumR / (float)m_gaussianCount;
        MR_LOG(LogTemp, Log,
               "[DIAG][RadiusDepth] count=%u maxRadius=%u depthAtMax=%.3f avgRadius=%.2f culled=%u "
               "activeDepth=[%u,%u,%u,%u,%u] wide30Depth=[%u,%u,%u,%u,%u]",
               m_gaussianCount, maxR, depthAtMax, avgR, culled,
               activeByDepth[0], activeByDepth[1], activeByDepth[2], activeByDepth[3], activeByDepth[4],
               wideByDepth[0], wideByDepth[1], wideByDepth[2], wideByDepth[3], wideByDepth[4]);
        fprintf(stderr,
               "[DIAG][RadiusDepth] maxRadius=%u depthAtMax=%.3f active=[%u,%u,%u,%u,%u] wide30=[%u,%u,%u,%u,%u]\n",
               maxR, depthAtMax, activeByDepth[0], activeByDepth[1], activeByDepth[2], activeByDepth[3], activeByDepth[4],
               wideByDepth[0], wideByDepth[1], wideByDepth[2], wideByDepth[3], wideByDepth[4]);

        // Re-begin for onRender.
        auto outputTex = m_render.getOutputTexture();
        cmdList->begin();
        if (outputTex)
        {
            cmdList->transitionResource(outputTex,
                RHI::EResourceUsage::None,
                RHI::EResourceUsage::UnorderedAccess);
        }
    }

    // ========================================================================
    // ROOT-CAUSE FIX ② verification: prove the cov2D ceiling clamp engages.
    // One readback of conicOpacity[] per FOV refresh. We reconstruct the max
    // eigenvalue of the ORIGINAL cov2D from its inverse (conic) and check it is
    // bounded by kCovCeil=800. overCeil800 (lambdaMax > 800) must be 0 for the
    // clamp to be active; over30000 is the pre-fix reference. Pure logging.
    // ========================================================================
    void FSplatPipeline::diagnoseCovCeil(RHI::IRHICommandList* cmdList)
    {
        using namespace RHI;
        if (!m_diagConic)
            m_diagConic = createStagingBuffer(m_device, m_gaussianCount * 4 * sizeof(float), "Diag_Conic");
        if (!m_diagConic) {
            MR_LOG(LogTemp, Error, "[DIAG] conic staging allocation failed");
            return;
        }

        const FSplatPreprocessOutput& out = m_preprocess.getOutput();
        cmdList->copyBuffer(m_diagConic, out.conicOpacity, m_gaussianCount * 4 * sizeof(float));
        cmdList->resourceBarrier();
        cmdList->end();
        cmdList->submitAndWait();

        float* c = reinterpret_cast<float*>(m_diagConic->map());
        if (!c) {
            MR_LOG(LogTemp, Error, "[DIAG] conic map failed");
            cmdList->begin();
            return;
        }
        // conic stored as vec4(conic.x, conic.y, conic.z, opacity) where
        //   conic.x = covZZ * detInv,  conic.z = covXX * detInv
        // so the smaller of (conic.x, conic.z) ~ 1 / (larger diagonal cov2D).
        float  minDiagConic = 1e30f;
        float  maxLambdaCov = 0.0f;   // max lambdaMax of raw cov2D, reconstructed from conic
        uint32 overCeil     = 0;      // lambdaMax > kCovCeil(800) => clamp leaked (should be 0)
        uint32 over30k      = 0;      // lambdaMax > 30000 (pre-fix reference)
        for (uint32 i = 0; i < m_gaussianCount; ++i) {
            float cx = c[i * 4 + 0];  // conic.x = covZZ / det
            float cy = c[i * 4 + 1];  // conic.y = -covXY / det
            float cz = c[i * 4 + 2];  // conic.z = covXX / det
            float d  = (cx < cz) ? cx : cz;
            if (d < minDiagConic) minDiagConic = d;
            // Reconstruct the max eigenvalue of the ORIGINAL cov2D from its
            // inverse (conic). inverse = [[cx,cy],[cy,cz]]; its eigenvalues are
            // 1/λ1 and 1/λ2, so λmax_cov = 1/(smaller inverse eigenvalue)
            // = 2 / (tr_inv - sqrt(tr_inv² - 4·det_inv)).
            float trInv   = cx + cz;
            float detInv  = cx * cz - cy * cy;
            float discInv = trInv * trInv - 4.0f * detInv;
            if (discInv > 0.0f) {
                float denom = trInv - sqrtf(discInv);
                if (denom > 1e-9f) {
                    float lambdaMax = 2.0f / denom;
                    if (lambdaMax > maxLambdaCov) maxLambdaCov = lambdaMax;
                    if (lambdaMax > 800.0f)       overCeil++;
                    if (lambdaMax > 30000.0f)     over30k++;
                }
            }
        }
        m_diagConic->unmap();

        MR_LOG(LogTemp, Log,
               "[DIAG][CovCeil] tanFovY=%.5f maxLambdaCov=%.1f overCeil800=%u (%.2f%%) "
               "over30000=%u (%.2f%%) minDiagConic=%.3e  [clamp: overCeil800==0 => fix active]",
               m_prevTanFovY, maxLambdaCov, overCeil,
               100.0f * (float)overCeil / (float)m_gaussianCount,
               over30k, 100.0f * (float)over30k / (float)m_gaussianCount,
               minDiagConic);
        fprintf(stderr,
                "[DIAG][CovCeil] tanFovY=%.5f maxLambdaCov=%.1f overCeil800=%u over30000=%u minDiagConic=%.3e\n",
                m_prevTanFovY, maxLambdaCov, overCeil, over30k, minDiagConic);

        // Re-begin for onRender (output texture needs GENERAL layout for imageStore).
        auto outputTex = m_render.getOutputTexture();
        cmdList->begin();
        if (outputTex) {
            cmdList->transitionResource(outputTex,
                RHI::EResourceUsage::None,
                RHI::EResourceUsage::UnorderedAccess);
        }
    }

    // ========================================================================
    // TEMP DIAGNOSTIC: read back real GPU state to pin down the blank-screen
    // cause. Logs: (1) how many tile ranges are non-empty, (2) for the first
    // N scheduled gaussians, whether their projected pointsXY lands inside the
    // viewport and whether their conic is positive-definite (required for any
    // splat to survive the power<=0 test in the render shader).
    // ========================================================================
    void FSplatPipeline::diagnoseRenderState(RHI::IRHICommandList* cmdList)
    {
        using namespace RHI;

        const uint32 kSampleSorted = 4096u;
        const uint32 sidSample = (m_realSortElements < kSampleSorted) ? m_realSortElements : kSampleSorted;

        // Allocate staging buffers once.
        if (!m_diagBuffersAllocated)
        {
            m_diagTR     = createStagingBuffer(m_device, m_numTiles * 2 * sizeof(uint32), "Diag_TR");
            m_diagSorted = createStagingBuffer(m_device, sidSample * sizeof(uint32),        "Diag_Sorted");
            m_diagSortedKeys = createStagingBuffer(m_device, sidSample * sizeof(uint64),    "Diag_SortedKeys");
            m_diagPXY    = createStagingBuffer(m_device, m_gaussianCount * 2 * sizeof(float), "Diag_PXY");
            m_diagConic  = createStagingBuffer(m_device, m_gaussianCount * 4 * sizeof(float), "Diag_Conic");
            if (!m_diagTR || !m_diagSorted || !m_diagSortedKeys || !m_diagPXY || !m_diagConic)
            {
                MR_LOG(LogTemp, Error, "[DIAG] staging buffer allocation failed");
                return;
            }
            m_diagBuffersAllocated = true;
        }

        const FSplatPreprocessOutput& out = m_preprocess.getOutput();

        // Copy everything we need into staging (all written earlier this frame).
        cmdList->copyBuffer(m_diagTR,     m_tileBoundaries.getTileRanges(), m_numTiles * 2 * sizeof(uint32));
        cmdList->copyBuffer(m_diagSorted, m_radixSort.getSortedValues(),    sidSample * sizeof(uint32));
        cmdList->copyBuffer(m_diagSortedKeys, m_radixSort.getSortedKeys(),  sidSample * sizeof(uint64));
        cmdList->copyBuffer(m_diagPXY,    out.pointsXY,    m_gaussianCount * 2 * sizeof(float));
        cmdList->copyBuffer(m_diagConic,  out.conicOpacity, m_gaussianCount * 4 * sizeof(float));
        cmdList->resourceBarrier();

        // Submit what we have, wait for completion, then inspect.
        cmdList->end();
        cmdList->submitAndWait();

        // ---- (1) tile ranges ----
        uint32* tr = reinterpret_cast<uint32*>(m_diagTR->map());
        uint32 nonEmpty = 0, totalCovered = 0;
        for (uint32 i = 0; i < m_numTiles; ++i)
        {
            uint32 s = tr[i * 2], e = tr[i * 2 + 1];
            if (s < e) { nonEmpty++; totalCovered += (e - s); }
        }
        uint32 cx = m_gridX / 2, cy = m_gridY / 2;
        uint32 centerTile = cy * m_gridX + cx;
        uint32 cs = tr[centerTile * 2], ce = tr[centerTile * 2 + 1];
        // a couple more sample tiles (top-left and middle-right)
        uint32 tl = 0;                       uint32 tlS = tr[tl*2],     tlE = tr[tl*2+1];
        uint32 mr = cy * m_gridX + (m_gridX-1); uint32 mrS = tr[mr*2], mrE = tr[mr*2+1];
        m_diagTR->unmap();

        MR_LOG(LogTemp, Log, "[DIAG] tileRanges: nonEmpty=%u/%u totalCovered=%u | center(%u,%u)=[%u,%u] tl=[%u,%u] mr=[%u,%u]",
               nonEmpty, m_numTiles, totalCovered, cx, cy, cs, ce, tlS, tlE, mrS, mrE);
        fprintf(stderr, "[DIAG] tileRanges: nonEmpty=%u/%u totalCovered=%u center=[%u,%u] tl=[%u,%u] mr=[%u,%u]\n",
                nonEmpty, m_numTiles, totalCovered, cs, ce, tlS, tlE, mrS, mrE);

        // ---- (2) scheduled gaussians: on-screen + conic sign ----
        uint32* sid   = reinterpret_cast<uint32*>(m_diagSorted->map());
        uint64* skeys = reinterpret_cast<uint64*>(m_diagSortedKeys->map());
        float*  pxy   = reinterpret_cast<float*>(m_diagPXY->map());
        float*  conic = reinterpret_cast<float*>(m_diagConic->map());

        // Raw dump of first 32 sorted (tileID, value) pairs to reveal sort corruption pattern.
        {
            char dumpBuf[1024];
            int pos = 0;
            pos += (int)snprintf(dumpBuf + pos, sizeof(dumpBuf) - pos, "[DIAG] first32 raw (tileID,value): ");
            for (uint32 i = 0; i < 32 && i < sidSample; ++i)
            {
                uint32 tileID = (uint32)(skeys[i] >> 32u);
                pos += (int)snprintf(dumpBuf + pos, sizeof(dumpBuf) - pos, "(%u,%u) ", tileID, sid[i]);
            }
            fprintf(stderr, "%s\n", dumpBuf);
        }

        uint32 onScreen = 0, conicOK = 0, garbageIdx = 0;
        float minX =  1e30f, maxX = -1e30f, minY =  1e30f, maxY = -1e30f;
        float badConicX = 0.0f, badConicZ = 0.0f; // remember one bad conic for logging
        bool  gotBad = false;
        for (uint32 i = 0; i < sidSample; ++i)
        {
            uint32 g = sid[i];
            if (g >= m_gaussianCount) { garbageIdx++; continue; }
            float px = pxy[g * 2], py = pxy[g * 2 + 1];
            float cx_ = conic[g * 4], cy_ = conic[g * 4 + 1], cz_ = conic[g * 4 + 2], op = conic[g * 4 + 3];
            if (px < minX) minX = px; if (px > maxX) maxX = px;
            if (py < minY) minY = py; if (py > maxY) maxY = py;
            if (px >= 0.0f && px < (float)m_imageWidth && py >= 0.0f && py < (float)m_imageHeight)
                onScreen++;
            if (cx_ > 0.0f && cz_ > 0.0f) conicOK++;
            else if (!gotBad) { badConicX = cx_; badConicZ = cz_; gotBad = true; }
        }
        m_diagSorted->unmap(); m_diagSortedKeys->unmap(); m_diagPXY->unmap(); m_diagConic->unmap();

        MR_LOG(LogTemp, Log, "[DIAG] sampled %u scheduled gaussians: onScreen=%u conicOK=%u garbageIdx=%u",
               sidSample, onScreen, conicOK, garbageIdx);
        MR_LOG(LogTemp, Log, "[DIAG] pointsXY X-range=[%.2f,%.2f] Y-range=[%.2f,%.2f] (viewport %ux%u); badConic sample=(%.4g,%.4g)",
               minX, maxX, minY, maxY, m_imageWidth, m_imageHeight, badConicX, badConicZ);
        fprintf(stderr, "[DIAG] sampled gaussians: onScreen=%u/%u conicOK=%u garbage=%u pxyX[%.1f,%.1f] pxyY[%.1f,%.1f] img=%ux%u\n",
                onScreen, sidSample, conicOK, garbageIdx, minX, maxX, minY, maxY, m_imageWidth, m_imageHeight);
        fprintf(stderr, "[DIAG] conic sign: conicOK=%u/%u (need both conic.x>0 AND conic.z>0); badConicSample=(%.4g,%.4g)\n",
                conicOK, sidSample, badConicX, badConicZ);

        // Restart the command list so onRender (transition + present) can continue.
        cmdList->begin();
    }

} // namespace MonsterRender::Splat
