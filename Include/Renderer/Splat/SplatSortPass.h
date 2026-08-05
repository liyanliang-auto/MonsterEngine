// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file SplatSortPass.h
 * @brief GPU sort pipeline pass classes for 3DGS rendering.
 * 
 * Four sequential passes:
 *   1. FSplatPrefixSumPass    - Blelloch scan on tilesTouched[]
 *   2. FSplatAssignKeysPass   - Generate 64-bit sort keys + 32-bit values
 *   3. FSplatRadixSortPass    - 4-pass × 8-bit LSD radix sort
 *   4. FSplatTileBoundariesPass - Detect tile boundaries in sorted keys
 */

#include "Core/CoreTypes.h"
#include "RHI/IRHIDevice.h"
#include "RHI/IRHICommandList.h"
#include "RHI/IRHIDescriptorSet.h"
#include "Renderer/Splat/SplatSortTypes.h"

namespace MonsterRender {
namespace Splat {

// ============================================================================
// FSplatPrefixSumPass - Blelloch Scan (Step 3.1)
// ============================================================================

/**
 * Computes a cumulative prefix sum of the tilesTouched[] array from the
 * preprocess pass output. Uses ping-pong buffers and ceil(log2(N)) iterations.
 * 
 * The final result buffer can be read back to determine totalSortElements
 * (required for later buffer allocation).
 */
class FSplatPrefixSumPass
{
public:
    FSplatPrefixSumPass();
    ~FSplatPrefixSumPass();

    /**
     * Initialize descriptor layout, pipeline, and ping-pong buffers.
     * @param device RHI device
     * @param maxElements Maximum number of Gaussians (= preprocess output count)
     */
    bool initialize(RHI::IRHIDevice* device, uint32 maxElements);

    /**
     * Execute the prefix sum computation.
     * @param cmdList Command list to record into
     * @param actualCount Actual number of valid elements (should be <= maxElements)
     * @return The last element value (= total sort entries needed)
     */
    uint32 execute(RHI::IRHICommandList* cmdList, uint32 actualCount);

    /** Get the buffer containing the final prefix sum result */
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> getResultBuffer() const;

    /** Get buffer A (for external tile copy / initial data write) */
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> getBufferA() const { return m_bufferA; }

    bool isInitialized() const { return m_bInitialized; }
    uint32 getMaxElements() const { return m_numElements; }

private:
    bool m_bInitialized = false;
    bool m_resultIsInB = false;  // True if final result is in bufferB
    uint32 m_numElements = 0;

    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_bufferA;
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_bufferB;
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_totalSumHost;

    MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSetLayout> m_setLayout;
    MonsterEngine::TSharedPtr<RHI::IRHIPipelineLayout> m_pipelineLayout;
    MonsterEngine::TSharedPtr<RHI::IRHIPipelineState> m_pipelineState;
    MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSet> m_descriptorSet;
};

// ============================================================================
// FSplatAssignKeysPass - Generate Sort Keys (Step 3.2)
// ============================================================================

/**
 * Generates sort key-value pairs from preprocess output and prefix sum.
 * For each Gaussian that survived preprocess, emits one entry per tile 
 * it touches. Each entry: 64-bit key (tileID<<32 | depthBits) + 32-bit value (index).
 */
class FSplatAssignKeysPass
{
public:
    FSplatAssignKeysPass() = default;
    ~FSplatAssignKeysPass();

    /**
     * Initialize descriptor layout, pipeline, and output key/value buffers.
     * @param device RHI device
     * @param totalSortElements Total entries (sum of tilesTouched, from prefix sum result)
     * @param gaussianCount Number of Gaussians to process
     */
    bool initialize(RHI::IRHIDevice* device, uint32 totalSortElements, uint32 gaussianCount);

    /**
     * Set input buffers from preprocess pass output and prefix sum pass.
     */
    void setInputBuffers(
        MonsterEngine::TSharedPtr<RHI::IRHIBuffer> prefixSum,
        MonsterEngine::TSharedPtr<RHI::IRHIBuffer> bbox,
        MonsterEngine::TSharedPtr<RHI::IRHIBuffer> depth);

    /**
     * Execute the key assignment compute shader.
     */
    void execute(RHI::IRHICommandList* cmdList, uint32 gaussianCount,
                 uint32 gridX, uint32 gridY);

    /** Get the generated sort keys buffer (uint64_t[]) */
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> getSortKeys() const { return m_sortKeys; }

    /** Get the generated sort values buffer (uint32_t[]) */
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> getSortValues() const { return m_sortValues; }

    uint32 getSortElementCount() const { return m_sortElementCount; }
    bool isInitialized() const { return m_bInitialized; }

private:
    bool m_bInitialized = false;
    uint32 m_sortElementCount = 0;
    uint32 m_gaussianCount = 0;

    // Output buffers
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_sortKeys;
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_sortValues;

    // Input buffers (stored for per-frame descriptor update in execute)
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_prefixSum;
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_bbox;
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_depth;

    // Pipeline resources
    MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSetLayout> m_setLayout;
    MonsterEngine::TSharedPtr<RHI::IRHIPipelineLayout> m_pipelineLayout;
    MonsterEngine::TSharedPtr<RHI::IRHIPipelineState> m_pipelineState;
    // Per-frame descriptor sets to avoid updating while cmd buffer is pending
    static constexpr uint32 kMaxFramesInFlight = 2;
    MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSet> m_descriptorSets[kMaxFramesInFlight];
    uint32 m_currentDsIndex = 0;
};

// ============================================================================
// FSplatRadixSortPass - LSD Radix Sort (Step 3.3)
// ============================================================================

/**
 * Sorts key-value pairs using an 8-bit LSD radix sort over 4 passes (32-bit key).
 * Based on VkRadixSort by Mirco Werner (MIT License).
 * 
 * Each pass: histogram (256-bin local + global) → scatter (subgroup prefix sums).
 * Ping-pongs between even/odd key/value buffers across passes.
 */
class FSplatRadixSortPass
{
public:
    FSplatRadixSortPass() = default;
    ~FSplatRadixSortPass();

    /**
     * Initialize histogram/scatter pipelines, descriptor layouts, and buffers.
     * @param device RHI device
     * @param maxSortElements Upper bound on sort elements (from prefix sum total)
     */
    bool initialize(RHI::IRHIDevice* device, uint32 maxSortElements);

    /**
     * Execute the 4-pass radix sort.
     * On entry, keysEven/valuesEven should contain the unsorted data 
     * (e.g., copied from AssignKeys output). After execution, final sorted 
     * data is in keysEven/valuesEven if finished on even pass.
     * 
     * @param cmdList Command list
     * @param numElements Actual number of sort entries
     * @return true if final result is in even buffers, false if in odd
     */
    bool execute(RHI::IRHICommandList* cmdList, uint32 numElements);

    /** Get the sorted key buffer */
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> getSortedKeys() const;

    /** Get the sorted value buffer */
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> getSortedValues() const;

    /** Get the even key buffer (for initial data copy-in from AssignKeys) */
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> getKeysEven()  const { return m_keysEven; }
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> getValuesEven() const { return m_valuesEven; }

    bool isInitialized() const { return m_bInitialized; }
    uint32 getMaxElements() const { return m_maxElements; }

private:
    bool createHistogramPipeline(RHI::IRHIDevice* device);
    bool createScatterPipeline(RHI::IRHIDevice* device);

    bool m_bInitialized = false;
    uint32 m_maxElements = 0;

    // Ping-pong key/value buffers (uint64_t keys, uint32 values)
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_keysEven;
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_keysOdd;
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_valuesEven;
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_valuesOdd;

    // Histogram buffer (BINS * maxWorkgroups * sizeof(uint32))
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_histogram;

    // Histogram pipeline
    MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSetLayout> m_histSetLayout;
    MonsterEngine::TSharedPtr<RHI::IRHIPipelineLayout> m_histPipelineLayout;
    MonsterEngine::TSharedPtr<RHI::IRHIPipelineState> m_histPipeline;
    MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSet> m_histDescriptorSetEven;   // SortKeys = keysEven
    MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSet> m_histDescriptorSetOdd;    // SortKeys = keysOdd

    // Scatter pipeline
    MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSetLayout> m_scatterSetLayout;
    MonsterEngine::TSharedPtr<RHI::IRHIPipelineLayout> m_scatterPipelineLayout;
    MonsterEngine::TSharedPtr<RHI::IRHIPipelineState> m_scatterPipeline;
    MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSet> m_scatterDescriptorSetEven; // even→odd
    MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSet> m_scatterDescriptorSetOdd;  // odd→even

    // Track which buffer has the final result
    bool m_finalInEven = true;
};

// ============================================================================
// FSplatTileBoundariesPass - Tile Boundary Detection (Step 3.4)
// ============================================================================

/**
 * Detects tile boundaries in the sorted key array.
 * After radix sort, entries are grouped by tileID (key >> 32). This pass
 * scans adjacent keys and writes per-tile [start, end) ranges for the
 * render pass to consume.
 * 
 * Output: ranges[tileID] = uvec2(startIndex, endIndexExclusive)
 */
class FSplatTileBoundariesPass
{
public:
    FSplatTileBoundariesPass() = default;
    ~FSplatTileBoundariesPass();

    /**
     * Initialize pipeline and tile ranges buffer.
     * @param device RHI device
     * @param numTiles Total tile count (gridX * gridY)
     * @param maxSortElements Upper bound on sort elements
     */
    bool initialize(RHI::IRHIDevice* device, uint32 numTiles, uint32 maxSortElements);

    /**
     * Execute tile boundary detection.
     * @param cmdList Command list
     * @param numSortElements Actual sorted entry count
     */
    void execute(RHI::IRHICommandList* cmdList, uint32 numSortElements);

    /** Get the tile ranges buffer (uvec2[] per tile) */
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> getTileRanges() const { return m_tileRanges; }

    /** Set the sorted keys buffer (from radix sort output) */
    void setSortedKeys(MonsterEngine::TSharedPtr<RHI::IRHIBuffer> keys)
    {
        m_sortedKeys = keys;
        // Only update the descriptor set that will be used by the next execute() call.
        // Updating ALL per-frame sets would touch the set still in use by the previous
        // frame's pending command buffer (VUID-03047).
        uint32 nextIndex = (m_currentDsIndex + 1) % kMaxFramesInFlight;
        if (m_descriptorSets[nextIndex] && m_sortedKeys)
        {
            m_descriptorSets[nextIndex]->updateStorageBuffer(
                ETileBoundariesBinding::SortKeys, m_sortedKeys, 0, 0);
        }
    }

    bool isInitialized() const { return m_bInitialized; }
    uint32 getNumTiles() const { return m_numTiles; }

private:
    bool m_bInitialized = false;
    uint32 m_numTiles = 0;

    // Input (external, set via setSortedKeys)
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_sortedKeys;

    // Output
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_tileRanges;

    // Pipeline
    MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSetLayout> m_setLayout;
    MonsterEngine::TSharedPtr<RHI::IRHIPipelineLayout> m_pipelineLayout;
    MonsterEngine::TSharedPtr<RHI::IRHIPipelineState> m_pipelineState;
    // Per-frame descriptor sets to avoid updating while cmd buffer is pending
    static constexpr uint32 kMaxFramesInFlight = 2;
    MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSet> m_descriptorSets[kMaxFramesInFlight];
    uint32 m_currentDsIndex = 0;
};

} // namespace Splat
} // namespace MonsterRender
