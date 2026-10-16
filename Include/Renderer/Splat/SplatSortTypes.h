// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file SplatSortTypes.h
 * @brief GPU data layout and binding definitions for the 3DGS sort pipeline.
 * 
 * Covers: Prefix Sum → Assign Keys → Radix Sort → Tile Boundaries.
 * All structs are designed for std140/std430 compatibility.
 */

#include "Core/CoreTypes.h"
#include <cstddef>

namespace MonsterRender {
namespace Splat {

// ============================================================================
// Prefix Sum Pass
// ============================================================================

/** Prefix sum push constants (12 bytes, packed, matches GLSL push_constant layout) */
struct FPrefixSumPushConstants
{
    uint32 step;         // Current step index (0..ceil(log2(n))-1)
    uint32 numElements;  // Total number of elements
    uint32 readFromA;    // 1 = read bufferA → write bufferB, 0 = read bufferB → write bufferA
};

static_assert(sizeof(FPrefixSumPushConstants) == 12, "FPrefixSumPushConstants must be 12 bytes");

/** Descriptor set bindings for prefix sum (Set 0) */
namespace EPrefixSumBinding
{
    constexpr uint32 BufferA = 0;  // storage buffer: uint[] (ping)
    constexpr uint32 BufferB = 1;  // storage buffer: uint[] (pong)
    constexpr uint32 Count    = 2;
}

// ============================================================================
// Assign Keys Pass
// ============================================================================

/** Assign keys push constants (32-byte host range, 28-byte GLSL fields). */
struct alignas(16) FAssignKeysPushConstants
{
    uint32 gaussianCount;   // Number of Gaussians to process
    uint32 gridX;           // Tile grid width = ceil(imageWidth / 16)
    uint32 gridY;           // Tile grid height = ceil(imageHeight / 16)
    uint32 maxSortElements; // Sort buffer capacity (total entries) for bounds check
    uint32 tightTileCulling = 0; ///< Identical to preprocess mode.
    uint32 imageWidth = 0; ///< Exact framebuffer width, including partial tiles.
    uint32 imageHeight = 0; ///< Exact framebuffer height.
    uint32 pad = 0; ///< Push range padding.
};

static_assert(sizeof(FAssignKeysPushConstants) == 32, "FAssignKeysPushConstants must be 32 bytes");
static_assert(offsetof(FAssignKeysPushConstants, tightTileCulling) == 16, "Assign culling ABI offset");
static_assert(offsetof(FAssignKeysPushConstants, imageHeight) == 24, "Assign framebuffer ABI offset");

/** Descriptor set bindings for assign keys (Set 0) */
namespace EAssignKeysBinding
{
    constexpr uint32 PrefixSum     = 0;  // storage buffer (read): uint[]

    // From preprocess output
    constexpr uint32 BBox          = 1;  // storage buffer (read): uvec4[] (rectMinX, rectMinY, rectMaxX, rectMaxY)
    constexpr uint32 Depth         = 2;  // storage buffer (read): float[]
    constexpr uint32 TilesTouched  = 3;  // storage buffer (read): uint[]

    // Output
    constexpr uint32 SortKeys      = 4;  // storage buffer (write): uint64_t[] (tile<<32 | depth)
    constexpr uint32 SortValues    = 5;  // storage buffer (write): uint32[] (Gaussian index)
    constexpr uint32 DispatchState = 6;
    constexpr uint32 ConicOpacity = 7; ///< Projected FP32 conic and opacity.
    constexpr uint32 PointsXY = 8; ///< Projected FP32 center.
    constexpr uint32 Count         = 9;
}

// ============================================================================
// Radix Sort Pass
// ============================================================================

/// Number of bins per radix pass (8 bits = 256 bins)
constexpr uint32 RADIX_SORT_BINS = 256;
/// Workgroup size for histogram/scatter passes
constexpr uint32 RADIX_SORT_WORKGROUP = 256;
/// Number of elements each thread processes per block
constexpr uint32 RADIX_SORT_BLOCKS_PER_WG = 32;

/** Supported exact-sort workloads; blocks32 remains the reference default. */
constexpr bool IsValidRadixSortBlocks(uint32 blocks)
{
    return blocks == 4 || blocks == 8 || blocks == 16 || blocks == 32;
}

/** Overflow-safe ceiling division; invalid workloads have no dispatch groups. */
constexpr uint32 RadixSortWorkgroupCount(uint32 elements, uint32 blocks)
{
    if (!IsValidRadixSortBlocks(blocks)) return 0;
    return elements / (RADIX_SORT_WORKGROUP * blocks) +
        (elements % (RADIX_SORT_WORKGROUP * blocks) != 0 ? 1u : 0u);
}

/** Prepare-dispatch ABI shared by regular and split-fusion producers. */
struct FSplatPrepareDispatchPushConstants
{
    uint32 activeCount; ///< Inclusive scan input extent.
    uint32 sortCapacity; ///< Key/value allocation capacity.
    uint32 gridX, gridY; ///< Raster tile grid.
    uint32 radixBlocks; ///< Validated blocks per radix workgroup.
};
static_assert(sizeof(FSplatPrepareDispatchPushConstants) == 20, "Prepare dispatch ABI size");
static_assert(offsetof(FSplatPrepareDispatchPushConstants, radixBlocks) == 16, "Prepare radix ABI offset");

/** Radix sort push constants (16 bytes) */
struct alignas(16) FRadixSortPushConstants
{
    uint32 numElements;           // Total sort entries
    uint32 shift;                 // Bit shift for this pass (0, 8, ..., 56)
    uint32 numWorkgroups;         // Actual direct groups or allocated GPU-count group bound
    uint32 numBlocksPerWorkgroup; // Validated blocks processed per workgroup (4, 8, 16 or 32)
};

static_assert(sizeof(FRadixSortPushConstants) == 16, "FRadixSortPushConstants must be 16 bytes");

/** Descriptor set bindings for radix sort histogram (Set 0) */
namespace ERadixHistogramBinding
{
    constexpr uint32 SortKeys    = 0;  // storage buffer (read): uint64_t[]
    constexpr uint32 Histogram   = 1;  // storage buffer (write): uint32[RADIX_SORT_BINS * numWorkgroups]
    constexpr uint32 DispatchState = 2;
    constexpr uint32 Count       = 3;
}

/** Descriptor set bindings for radix sort scatter (Set 0)
 *  MUST match the `binding = N` layout in Shaders/Splat/Sort/splat_radix_scatter.comp:
 *    0 = KeysIn, 1 = KeysOut, 2 = ValuesIn, 3 = ValuesOut, 4 = HistogramBuf
 */
namespace ERadixScatterBinding
{
    constexpr uint32 SortKeysIn    = 0;  // storage buffer (read):  uint64_t[]   (binding 0: KeysIn)
    constexpr uint32 SortKeysOut   = 1;  // storage buffer (write): uint64_t[]   (binding 1: KeysOut)
    constexpr uint32 SortValuesIn  = 2;  // storage buffer (read):  uint32[]      (binding 2: ValuesIn)
    constexpr uint32 SortValuesOut = 3;  // storage buffer (write): uint32[]      (binding 3: ValuesOut)
    constexpr uint32 Histogram     = 4;  // storage buffer (read):  uint32[RADIX_SORT_BINS * numWorkgroups] (binding 4: HistogramBuf)
    constexpr uint32 DispatchState = 5;
    constexpr uint32 Count         = 6;
}

// ============================================================================
// Tile Boundaries Pass
// ============================================================================

/** Tile boundaries push constants (12 bytes, matches GLSL push_constant layout) */
struct FTileBoundariesPushConstants
{
    uint32 numSortElements;  // Total number of sorted entries
    uint32 numTiles;         // gridX * gridY
    uint32 mode;             // 0 = boundary detection, 1 = clear all tiles to (0,0)
};

static_assert(sizeof(FTileBoundariesPushConstants) == 12, "FTileBoundariesPushConstants must be 12 bytes");

/** Descriptor set bindings for tile boundaries (Set 0) */
namespace ETileBoundariesBinding
{
    constexpr uint32 SortKeys = 0;  // storage buffer (read): uint64_t[] (sorted by tile→depth)
    constexpr uint32 TileRanges = 1; // storage buffer (write): uvec2[] (start, end per tile)
    constexpr uint32 DispatchState = 2;
    constexpr uint32 Count      = 3;
}

} // namespace Splat
} // namespace MonsterRender
