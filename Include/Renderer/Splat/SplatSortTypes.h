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

/** Assign keys push constants (16 bytes) */
struct alignas(16) FAssignKeysPushConstants
{
    uint32 gaussianCount;  // Number of Gaussians to process
    uint32 gridX;          // Tile grid width = ceil(imageWidth / 16)
    uint32 gridY;          // Tile grid height = ceil(imageHeight / 16)
    float32 maxDepth;      // Far plane depth for depth quantization
};

static_assert(sizeof(FAssignKeysPushConstants) == 16, "FAssignKeysPushConstants must be 16 bytes");

/** Descriptor set bindings for assign keys (Set 0) */
namespace EAssignKeysBinding
{
    constexpr uint32 PrefixSum     = 0;  // storage buffer (read): uint[]

    // From preprocess output
    constexpr uint32 BBox          = 1;  // storage buffer (read): uvec4[] (rectMinY, rectMinX, rectMaxY, rectMaxX)
    constexpr uint32 Depth         = 2;  // storage buffer (read): float[]
    constexpr uint32 TilesTouched  = 3;  // storage buffer (read): uint[]

    // Output
    constexpr uint32 SortKeys      = 4;  // storage buffer (write): uint64_t[] (tile<<32 | depth)
    constexpr uint32 SortValues    = 5;  // storage buffer (write): uint32[] (Gaussian index)
    constexpr uint32 Count         = 6;
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

/** Radix sort push constants (16 bytes) */
struct alignas(16) FRadixSortPushConstants
{
    uint32 numElements;           // Total sort entries
    uint32 shift;                 // Bit shift for this pass (0, 8, 16, 24)
    uint32 numWorkgroups;         // Number of workgroups to dispatch
    uint32 numBlocksPerWorkgroup; // Blocks processed per workgroup (= RADIX_SORT_BLOCKS_PER_WG)
};

static_assert(sizeof(FRadixSortPushConstants) == 16, "FRadixSortPushConstants must be 16 bytes");

/** Descriptor set bindings for radix sort histogram (Set 0) */
namespace ERadixHistogramBinding
{
    constexpr uint32 SortKeys    = 0;  // storage buffer (read): uint64_t[]
    constexpr uint32 Histogram   = 1;  // storage buffer (write): uint32[RADIX_SORT_BINS * numWorkgroups]
    constexpr uint32 Count       = 2;
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
    constexpr uint32 Count         = 5;
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
    constexpr uint32 Count      = 2;
}

} // namespace Splat
} // namespace MonsterRender
