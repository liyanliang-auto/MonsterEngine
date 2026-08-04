// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file FXAATypes.h
 * @brief FXAA (Fast Approximate Anti-Aliasing) type definitions
 *
 * Implements NVIDIA FXAA 3.11 algorithm for spatial anti-aliasing.
 * Reference: UE5 FXAAShader.usf, Fxaa3_11.ush
 *
 * FXAA is mutually exclusive with TAA:
 *   - TAA (Temporal Anti-Aliasing) takes priority when both are enabled
 *   - FXAA only runs when TAA is disabled
 *   - Default: TAA enabled, FXAA disabled
 *
 * Quality Presets:
 *   - Preset 0: Fastest (mobile/low-end)
 *   - Preset 2: Balanced (default, recommended)
 *   - Preset 5: Highest quality (high-end PC)
 */

#include "Math/Vector2D.h"

namespace MonsterEngine
{
namespace PostProcess
{

/**
 * FXAA Configuration
 *
 * Controls FXAA quality and behavior.
 * Default settings provide balanced quality/performance (Preset 2).
 */
struct FFXAAConfig
{
    /**
     * Enable FXAA anti-aliasing
     * Default: false (TAA is preferred)
     * NOTE: FXAA only runs when TAA is disabled
     */
    bool EnableFXAA = false;

    /**
     * Quality preset (0-5)
     * 0 = Fastest (QUALITY__PRESET 10, PC_CONSOLE)
     * 1 = Fast    (QUALITY__PRESET 10, PC)
     * 2 = Medium  (QUALITY__PRESET 13, PC) [DEFAULT]
     * 3 = High    (QUALITY__PRESET 15, PC)
     * 4 = Higher  (QUALITY__PRESET 29, PC)
     * 5 = Highest (QUALITY__PRESET 39, PC)
     */
    int32 Preset = 2;

    /**
     * Sub-pixel aliasing removal amount (0.0 - 1.0)
     * 0.0  = No sub-pixel AA
     * 0.75 = Default (recommended)
     * 1.0  = Maximum sub-pixel AA
     */
    float QualitySubpix = 0.75f;

    /**
     * Edge detection threshold (0.063 - 0.333)
     * Lower = more edges detected (slower, higher quality)
     * Higher = fewer edges detected (faster, lower quality)
     * 
     * Recommended values:
     *   0.063 = High quality (detects subtle edges)
     *   0.166 = Balanced [DEFAULT]
     *   0.333 = Performance (only obvious edges)
     */
    float QualityEdgeThreshold = 0.166f;

    /**
     * Minimum edge detection threshold
     * Prevents FXAA from running on very low contrast areas
     * 
     * Recommended: 0.0833 (1/12)
     * Range: 0.0312 (1/32) - 0.0833 (1/12)
     */
    float QualityEdgeThresholdMin = 0.0833f;
};

/**
 * FXAA Uniform Buffer (std140 layout)
 *
 * GPU-side parameters for FXAA shader.
 * Size: 64 bytes (aligned for optimal GPU access)
 */
struct FFXAAUniformBuffer
{
    /**
     * Reciprocal frame size: (1.0 / width, 1.0 / height)
     * Used for texture coordinate offsets in FXAA algorithm
     */
    alignas(16) FVector2f RcpFrame;

    /**
     * Sub-pixel aliasing removal amount (0.0 - 1.0)
     * Copied from FFXAAConfig::QualitySubpix
     */
    float QualitySubpix;

    /**
     * Edge detection threshold
     * Copied from FFXAAConfig::QualityEdgeThreshold
     */
    float QualityEdgeThreshold;

    /**
     * Minimum edge detection threshold
     * Copied from FFXAAConfig::QualityEdgeThresholdMin
     */
    float QualityEdgeThresholdMin;

    /**
     * Quality preset (0-5)
     * Copied from FFXAAConfig::Preset
     * NOTE: Currently unused in shader (preset is baked at compile time)
     */
    int32 Preset;

    /**
     * Padding to align struct to 64 bytes
     * Required for std140 uniform buffer layout
     */
    float Padding[10];
};

// Compile-time validation of UBO size
static_assert(sizeof(FFXAAUniformBuffer) == 64, 
    "FFXAAUniformBuffer must be 64 bytes for std140 layout");

// Compile-time validation of field offsets
static_assert(offsetof(FFXAAUniformBuffer, RcpFrame) == 0,
    "RcpFrame must be at offset 0");
static_assert(offsetof(FFXAAUniformBuffer, QualitySubpix) == 8,
    "QualitySubpix must be at offset 8");
static_assert(offsetof(FFXAAUniformBuffer, QualityEdgeThreshold) == 12,
    "QualityEdgeThreshold must be at offset 12");
static_assert(offsetof(FFXAAUniformBuffer, QualityEdgeThresholdMin) == 16,
    "QualityEdgeThresholdMin must be at offset 16");
static_assert(offsetof(FFXAAUniformBuffer, Preset) == 20,
    "Preset must be at offset 20");

} // namespace PostProcess
} // namespace MonsterEngine
