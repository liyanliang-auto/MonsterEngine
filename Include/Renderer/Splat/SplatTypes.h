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

namespace MonsterRender {
namespace Splat {

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
// 16 bytes total
// ============================================================================
struct alignas(16) FPreprocessPushConstants
{
    uint32 gaussianCount;     // Number of gaussians to process
    float32 nearPlane;        // Near clip distance
    float32 farPlane;         // Far clip distance
    uint32 culling;           // 0 = disabled, 1 = enabled
};

static_assert(sizeof(FPreprocessPushConstants) == 16, "FPreprocessPushConstants must be 16 bytes");

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
    constexpr uint32 Count          = 6;
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
