// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file SplatTypes.h
 * @brief GPU-side data layout definitions for the 3DGS splat rendering pipeline.
 * 
 * Defines the CPU-side structs that mirror GLSL buffer layouts in 
 * splat_preprocess.comp. Layouts are std430 (storage buffers) and 
 * std140 (uniform buffers) compatible.
 */

#include "Core/CoreTypes.h"
#include "RHI/RHIDefinitions.h"
#include <cstdlib>
#include <cstddef>
#include <string>

namespace MonsterRender {
namespace Splat {

/** Resolve generated shader output without copying binaries into the source tree. */
inline std::string SplatShaderPath(const char* fallback) {
    const char* directory = std::getenv("MONSTER_SPLAT_SHADER_DIR");
#ifdef MONSTER_SPLAT_DEFAULT_SHADER_DIR
    if (!directory || !*directory) directory = MONSTER_SPLAT_DEFAULT_SHADER_DIR;
#endif
    if (!directory || !*directory) return fallback;
    const std::string path(fallback);
    return std::string(directory) + "/" + path.substr(path.find_last_of("/\\") + 1);
}
/** Shared GPU count/dispatch state layout; indirect triples are four-byte aligned. */
namespace ESplatDispatchState {
    constexpr uint32 Bytes = 128;
    constexpr uint32 AssignOffset = 32, RadixOffset = 44, BoundaryOffset = 56, RenderOffset = 68;
}

/** Projection RGB storage ABI. Packed RGB21F retains sign, 8 exponent and 12 mantissa bits per channel. */
namespace ESplatProjectionColor {
    constexpr uint32 FP32 = 0, RGB21F = 1;
    constexpr uint32 Stride(uint32 mode) { return mode == RGB21F ? 8u : 16u; }
    constexpr uint64_t Bytes(uint64_t capacity, uint32 mode) { return capacity * Stride(mode); }
}

/** Controls applied to both preprocess and blend passes each frame. */
struct FSplatRenderSettings
{
    float32 nearPlane = 0.1f;
    float32 farPlane = 1000.0f;
    bool distanceCulling = true;
    bool wireframe = false;
    float32 gaussianScale = 1.0f;
    uint32 tightTileCulling = 0; ///< 0=baseline, 1=tight rectangle, 2=conservative tile test.
    uint32 rasterSharedMemoryMode = 0; ///< 0=original Raster, 1=FP32 shared-memory batches of 64.
    float32 fusionRelativeRGBAError = 0.4f; ///< Explicit continuous per-channel L2 tolerance; image gates are separate.
};

// ============================================================================
// Camera Uniform Buffer (matches splat_preprocess.comp Set 0, Binding 5)
// std140 layout, 176 bytes
// ============================================================================
struct alignas(16) FCameraUniforms
{
    float32 viewMatrix[16];   // mat4, column-major (GLM style), offset 0,   64 bytes
    float32 projMatrix[16];   // mat4, column-major (GLM style), offset 64,  64 bytes
    float32 camPos[4];        // vec4 (w unused),               offset 128, 16 bytes
    float32 focalX;           // float,                         offset 144
    float32 focalY;           // float,                         offset 148
    float32 tanFovX;          // float,                         offset 152
    float32 tanFovY;          // float,                         offset 156
    int32 imageWidth;         // int,                           offset 160
    int32 imageHeight;        // int,                           offset 164
    int32 shDegree;           // int,                           offset 168
    int32 pad0;               // padding to align to 16,        offset 172 -> total 176
};

static_assert(sizeof(FCameraUniforms) == 176, "FCameraUniforms must be 176 bytes (std140)");
static_assert(alignof(FCameraUniforms) == 16, "FCameraUniforms must be 16-byte aligned");

// ============================================================================
// Preprocess Push Constants (matches splat_preprocess.comp)
// 48 bytes total (40 bytes of fields, padded to 16-byte alignment)
// ============================================================================
struct alignas(16) FPreprocessPushConstants
{
    uint32 gaussianCount;     // Number of gaussians to process
    float32 nearPlane;        // Near clip distance
    float32 farPlane;         // Far clip distance
    uint32 culling;           // 0 = disabled, 1 = enabled

    float32 gaussianScale;    // Multiplier for 3D covariance scale
    uint32 useActiveIndices = 0; ///< Source indirection enabled.
    uint32 sourceCapacity = 0; ///< Source-buffer index bound.
    uint32 shStride = 0; ///< Coefficients per Gaussian, excluding RGB factor.
    uint32 tightTileCulling = 0; ///< Shared count/emit mode at offset 32.
    uint32 shStorageMode = 0; ///< 0=FP32, 1=packed binary16 pairs, offset 36.
    uint32 projectionColorStorageMode = 0; ///< 0=FP32 vec4, 1=packed RGB21F; offset 40.
    uint32 pad = 0; ///< Push range padding.
};

static_assert(sizeof(FPreprocessPushConstants) == 48, "FPreprocessPushConstants must be 48 bytes");
static_assert(offsetof(FPreprocessPushConstants, projectionColorStorageMode) == 40, "Projection color storage ABI offset");
static_assert(offsetof(FPreprocessPushConstants, shStorageMode) == 36, "Preprocess SH storage ABI offset");
static_assert(offsetof(FPreprocessPushConstants, tightTileCulling) == 32, "Preprocess culling ABI offset");

// ============================================================================
// Binding point definitions for descriptor sets
// ============================================================================

/** Set 0: Gaussian input data + Camera UBO (6 bindings) */
namespace EInputBinding
{
    constexpr uint32 Positions      = 0;   // storage buffer: vec4[]
    constexpr uint32 Scales         = 1;   // storage buffer: vec4[]
    constexpr uint32 Rotations      = 2;   // storage buffer: vec4[]
    constexpr uint32 Opacities      = 3;   // storage buffer: float[]
    constexpr uint32 SHCoefficients = 4;   // storage buffer: float[]
    constexpr uint32 Camera         = 5;   // uniform buffer: FCameraUniforms
    constexpr uint32 ActiveIndices  = 6;
    constexpr uint32 Count          = 7;
}

/** Set 1: Preprocess output buffers (7 bindings) */
namespace EOutputBinding
{
    constexpr uint32 Radii         = 0;   // storage buffer: int[]
    constexpr uint32 Depth         = 1;   // storage buffer: float[]
    constexpr uint32 RGB           = 2;   // storage buffer: vec4[]
    constexpr uint32 ConicOpacity  = 3;   // storage buffer: vec4[]
    constexpr uint32 PointsXY      = 4;   // storage buffer: vec2[]
    constexpr uint32 TilesTouched  = 5;   // storage buffer: uint[]
    constexpr uint32 BBox          = 6;   // storage buffer: uvec4[]
    constexpr uint32 Count         = 7;
}

} // namespace Splat
} // namespace MonsterRender
