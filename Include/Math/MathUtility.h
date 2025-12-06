// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file MathUtility.h
 * @brief Math constants and utility definitions
 * 
 * This file defines mathematical constants for both float and double precision,
 * as well as threshold values for numerical comparisons.
 */

#include "MathFwd.h"
#include <cmath>
#include <limits>
#include <algorithm>

namespace MonsterEngine
{
namespace Math
{

// ============================================================================
// Mathematical Constants - Float Precision
// ============================================================================

/** Pi constant (float) */
#define MR_PI                       (3.1415926535897932f)

/** Small number for float comparisons */
#define MR_SMALL_NUMBER             (1.e-8f)

/** Kinda small number for float comparisons (less strict) */
#define MR_KINDA_SMALL_NUMBER       (1.e-4f)

/** Big number for float */
#define MR_BIG_NUMBER               (3.4e+38f)

/** Euler's number (float) */
#define MR_EULERS_NUMBER            (2.71828182845904523536f)

/** Golden ratio (float) */
#define MR_GOLDEN_RATIO             (1.6180339887498948482045868343656381f)

/** Maximum float value */
#define MR_MAX_FLT                  (3.402823466e+38F)

// ============================================================================
// Mathematical Constants - Double Precision
// ============================================================================

/** Pi constant (double) */
#define MR_DOUBLE_PI                (3.141592653589793238462643383279502884197169399)

/** Small number for double comparisons */
#define MR_DOUBLE_SMALL_NUMBER      (1.e-8)

/** Kinda small number for double comparisons */
#define MR_DOUBLE_KINDA_SMALL_NUMBER (1.e-4)

/** Big number for double */
#define MR_DOUBLE_BIG_NUMBER        (3.4e+38)

/** Euler's number (double) */
#define MR_DOUBLE_EULERS_NUMBER     (2.7182818284590452353602874713526624977572)

/** Golden ratio (double) */
#define MR_DOUBLE_GOLDEN_RATIO      (1.6180339887498948482045868343656381)

// ============================================================================
// Derived Constants - Float
// ============================================================================

/** Inverse of Pi (float) */
#define MR_INV_PI                   (0.31830988618f)

/** Half Pi (float) */
#define MR_HALF_PI                  (1.57079632679f)

/** Two Pi (float) */
#define MR_TWO_PI                   (6.28318530717f)

/** Pi squared (float) */
#define MR_PI_SQUARED               (9.86960440108f)

/** Square root of 2 (float) */
#define MR_SQRT_2                   (1.4142135623730950488016887242097f)

/** Square root of 3 (float) */
#define MR_SQRT_3                   (1.7320508075688772935274463415059f)

/** Inverse square root of 2 (float) */
#define MR_INV_SQRT_2               (0.70710678118654752440084436210485f)

/** Inverse square root of 3 (float) */
#define MR_INV_SQRT_3               (0.57735026918962576450914878050196f)

// ============================================================================
// Derived Constants - Double
// ============================================================================

/** Inverse of Pi (double) */
#define MR_DOUBLE_INV_PI            (0.31830988618379067154)

/** Half Pi (double) */
#define MR_DOUBLE_HALF_PI           (1.57079632679489661923)

/** Two Pi (double) */
#define MR_DOUBLE_TWO_PI            (6.28318530717958647692)

/** Pi squared (double) */
#define MR_DOUBLE_PI_SQUARED        (9.86960440108935861883)

/** Square root of 2 (double) */
#define MR_DOUBLE_SQRT_2            (1.4142135623730950488016887242097)

/** Square root of 3 (double) */
#define MR_DOUBLE_SQRT_3            (1.7320508075688772935274463415059)

// ============================================================================
// Threshold Constants for Numerical Comparisons
// ============================================================================

/** Delta for general float comparisons */
#define MR_DELTA                    (0.00001f)

/** Delta for general double comparisons */
#define MR_DOUBLE_DELTA             (0.00001)

/** Normal threshold for float */
#define MR_FLOAT_NORMAL_THRESH      (0.0001f)

/** Normal threshold for double */
#define MR_DOUBLE_NORMAL_THRESH     (0.0001)

/** Threshold for points being the same (float) */
#define MR_THRESH_POINTS_ARE_SAME           (0.00002f)

/** Threshold for points being near (float) */
#define MR_THRESH_POINTS_ARE_NEAR           (0.015f)

/** Threshold for normals being the same (float) */
#define MR_THRESH_NORMALS_ARE_SAME          (0.00002f)

/** Threshold for vectors being near (float) */
#define MR_THRESH_VECTORS_ARE_NEAR          (0.0004f)

/** Threshold for zero norm squared (float) */
#define MR_THRESH_ZERO_NORM_SQUARED         (0.0001f)

/** Threshold for normals being parallel (float) - cos(1 degree) */
#define MR_THRESH_NORMALS_ARE_PARALLEL      (0.999845f)

/** Threshold for normals being orthogonal (float) - cos(89 degrees) */
#define MR_THRESH_NORMALS_ARE_ORTHOGONAL    (0.017455f)

/** Allowed error for normalized vector (float) */
#define MR_THRESH_VECTOR_NORMALIZED         (0.01f)

/** Allowed error for normalized quaternion (float) */
#define MR_THRESH_QUAT_NORMALIZED           (0.01f)

// ============================================================================
// NaN Diagnostic Configuration
// ============================================================================

#ifndef ENABLE_NAN_DIAGNOSTIC
    #if defined(_DEBUG) || defined(DEBUG)
        #define ENABLE_NAN_DIAGNOSTIC 1
    #else
        #define ENABLE_NAN_DIAGNOSTIC 0
    #endif
#endif

// ============================================================================
// Inline Macros
// ============================================================================

#ifndef FORCEINLINE
    #if defined(_MSC_VER)
        #define FORCEINLINE __forceinline
    #elif defined(__GNUC__) || defined(__clang__)
        #define FORCEINLINE inline __attribute__((always_inline))
    #else
        #define FORCEINLINE inline
    #endif
#endif

#ifndef CONSTEXPR
    #define CONSTEXPR constexpr
#endif

// ============================================================================
// Nodiscard Macro
// ============================================================================

#ifndef MR_NODISCARD
    #if __cplusplus >= 201703L
        #define MR_NODISCARD [[nodiscard]]
    #else
        #define MR_NODISCARD
    #endif
#endif

} // namespace Math
} // namespace MonsterEngine
