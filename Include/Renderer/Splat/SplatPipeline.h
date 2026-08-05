/**
 * @file SplatPipeline.h
 * @brief 3DGS rendering pipeline orchestrator.
 * 
 * Chains all 6 compute passes (Preprocess → PrefixSum → AssignKeys → 
 * RadixSort → TileBoundaries → Render) into a single execute() call.
 * 
 * Usage:
 *   1. device->waitForIdle() before initializing
 *   2. FSplatPLYLoader::loadAndUpload() → FSplatGPUData
 *   3. SplatPipeline::initialize(device, count, width, height)
 *   4. pipeline.setGaussianData(data)
 *   5. pipeline.setCamera(camera)
 *   6. pipeline.execute(cmdList) → output texture
 */

#pragma once

#include "Core/CoreMinimal.h"
#include "Core/CoreTypes.h"
#include "RHI/IRHIDevice.h"
#include "RHI/IRHICommandList.h"
#include "Renderer/Splat/SplatTypes.h"
#include "Renderer/Splat/SplatPLYLoader.h"
#include "Renderer/Splat/SplatPass.h"
#include "Renderer/Splat/SplatSortPass.h"
#include "Renderer/Splat/SplatRenderPass.h"

namespace MonsterRender::Splat
{

    /**
     * FSplatPipeline — Full 3DGS rendering pipeline orchestrator.
     * 
     * Owns all 6 sub-passes and orchestrates their execution. On first 
     * execute(), sort passes are lazily initialized after the prefix sum 
     * determines the required sort element count. Subsequent frames reuse 
     * the same buffer allocations.
     * 
     * Thread safety: Not thread-safe. All calls must be from the render thread.
     */
    class FSplatPipeline
    {
    public:
        FSplatPipeline() = default;
        ~FSplatPipeline();

        /**
         * Initialize the preprocess, tile boundaries, and render passes.
         * Sort passes (prefix sum, assign keys, radix sort) are initialized 
         * lazily on first execute() since they depend on the prefix sum result.
         * 
         * @param device              RHI device
         * @param gaussianCount       Total number of Gaussians in the scene
         * @param imageWidth          Output image width in pixels
         * @param imageHeight         Output image height in pixels
         * @param maxTilesPerGaussian Conservative upper bound on tiles per Gaussian
         *                            (default 256, used to size sort buffers)
         */
        bool initialize(RHI::IRHIDevice* device, uint32 gaussianCount,
                        uint32 imageWidth, uint32 imageHeight,
                        uint32 maxTilesPerGaussian = 256);

        /**
         * Set the Gaussian input data (from PLY loader).
         * Must be called before execute().
         */
        void setGaussianData(const FSplatGPUData& data);

        /**
         * Set the camera parameters for this frame.
         * Must be called before execute(), can change every frame.
         */
        void setCamera(const FCameraUniforms& camera);

        /**
         * Execute the full 6-pass pipeline.
         * Returns the output texture containing the rendered 3DGS image.
         * 
         * On first call, sort passes are lazily initialized after prefix sum.
         * Subsequent calls skip initialization and reuse existing buffers.
         * 
         * @param cmdList Command list to record all dispatches into
         * @return Output texture (rgba8, UnorderedAccess | ShaderResource)
         */
        MonsterEngine::TSharedPtr<RHI::IRHITexture> execute(RHI::IRHICommandList* cmdList);

        /** Check if pipeline is initialized */
        bool isInitialized() const { return m_bInitialized; }

        /** Get the output texture (for layout management) */
        MonsterEngine::TSharedPtr<RHI::IRHITexture> getOutputTexture() const
        {
            return m_render.getOutputTexture();
        }

        /** Get the Gaussian count */
        uint32 getGaussianCount() const { return m_gaussianCount; }

        /** Get output image dimensions */
        uint32 getImageWidth()  const { return m_imageWidth; }
        uint32 getImageHeight() const { return m_imageHeight; }

    private:
        /**
         * Lazily initialize the sort passes (PrefixSum, AssignKeys, RadixSort).
         * Called on the first execute() after the device is available.
         */
        bool lazyInitSortPasses(RHI::IRHIDevice* device);

        /**
         * On first frame: run preprocess+prefixSum, read back totalTiles from 
         * staging buffer, initialize sort passes with the real element count.
         * On subsequent frames: no-op (returns true immediately).
         */
        bool ensureSortPassesInitialized(RHI::IRHICommandList* cmdList);

        // ================================================================
        // Sub-passes (in execution order)
        // ================================================================

        FSplatPreprocessPass     m_preprocess;        // Phase 2: 3D covariance, frustum, SH
        FSplatPrefixSumPass      m_prefixSum;         // Phase 3.1: Blelloch scan
        FSplatAssignKeysPass     m_assignKeys;        // Phase 3.2: Generate sort keys
        FSplatRadixSortPass      m_radixSort;         // Phase 3.3: 4-pass radix sort
        FSplatTileBoundariesPass m_tileBoundaries;    // Phase 3.4: Tile range detection
        FSplatRenderPass         m_render;            // Phase 4: EWA alpha blend

        // ================================================================
        // Pipeline state
        // ================================================================

        bool m_bInitialized         = false;
        bool m_sortPassesInitialized = false;

        // Device reference (for staging buffer creation and readback)
        RHI::IRHIDevice* m_device   = nullptr;

        uint32 m_gaussianCount      = 0;
        uint32 m_imageWidth         = 0;
        uint32 m_imageHeight        = 0;

        // Tile grid dimensions
        uint32 m_gridX              = 0;
        uint32 m_gridY              = 0;
        uint32 m_numTiles           = 0;

        // Sort buffer sizing (conservative upper bound)
        uint32 m_maxSortElements    = 0;

        // Real sort elements (read back from GPU after first prefix sum)
        uint32 m_realSortElements   = 0;

        // Staging buffer for GPU readback (8 bytes: prefix sum last + tilesTouched last)
        MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_stagingBuffer;

        // Input data readiness flags
        bool m_hasGaussianData      = false;
        bool m_hasCamera            = false;

        // ================================================================
        // TEMP diagnostic (blank-screen investigation) — remove after root cause found
        // ================================================================
        bool m_diagDone             = false;
        bool m_diagBuffersAllocated = false;
        MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_diagTR;      // tileRanges (uvec2[])
        MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_diagSorted;  // first N sortedIds (uint[])
        MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_diagSortedKeys; // first N sorted keys (uint64)
        MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_diagPXY;     // full pointsXY (vec2[])
        MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_diagConic;   // full conicOpacity (vec4[])

        /** Read back real GPU state after the 6 passes and log it (first frame only). */
        void diagnoseRenderState(RHI::IRHICommandList* cmdList);
    };

} // namespace MonsterRender::Splat
