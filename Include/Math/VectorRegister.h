// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file VectorRegister.h
 * @brief SIMD platform abstraction layer
 * 
 * This file provides platform-specific SIMD type definitions and includes
 * the appropriate implementation header based on the target platform.
 * Following UE5's VectorRegister pattern for transparent SIMD abstraction.
 */

#include "MathFwd.h"
#include "MathUtility.h"

// ============================================================================
// Platform Detection and SIMD Capability Flags
// ============================================================================

// Detect platform and set appropriate flags
#if defined(_MSC_VER)
    #define MR_PLATFORM_WINDOWS 1
#else
    #define MR_PLATFORM_WINDOWS 0
#endif

#if defined(__GNUC__) || defined(__clang__)
    #define MR_PLATFORM_POSIX 1
#else
    #define MR_PLATFORM_POSIX 0
#endif

// SIMD capability detection
#if defined(__AVX2__)
    #define MR_PLATFORM_MATH_USE_AVX2 1
#else
    #define MR_PLATFORM_MATH_USE_AVX2 0
#endif

#if defined(__AVX__) || MR_PLATFORM_MATH_USE_AVX2
    #define MR_PLATFORM_MATH_USE_AVX 1
#else
    #define MR_PLATFORM_MATH_USE_AVX 0
#endif

#if defined(__SSE4_1__)
    #define MR_PLATFORM_MATH_USE_SSE4_1 1
#else
    #define MR_PLATFORM_MATH_USE_SSE4_1 0
#endif

// SSE2 is baseline for x64
#if defined(_M_X64) || defined(__x86_64__) || defined(__SSE2__)
    #define MR_PLATFORM_ENABLE_VECTORINTRINSICS 1
#else
    #define MR_PLATFORM_ENABLE_VECTORINTRINSICS 0
#endif

// ARM NEON detection
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    #define MR_PLATFORM_ENABLE_VECTORINTRINSICS_NEON 1
#else
    #define MR_PLATFORM_ENABLE_VECTORINTRINSICS_NEON 0
#endif

// DirectXMath availability (Windows only, optional)
#if MR_PLATFORM_WINDOWS && defined(WITH_DIRECTXMATH)
    #define MR_WITH_DIRECTXMATH 1
#else
    #define MR_WITH_DIRECTXMATH 0
#endif

// ============================================================================
// Include Platform-Specific Implementation
// ============================================================================

#if MR_WITH_DIRECTXMATH
    #include "MonsterMathDirectX.h"
#elif MR_PLATFORM_ENABLE_VECTORINTRINSICS_NEON
    #include "MonsterMathNeon.h"
#elif MR_PLATFORM_ENABLE_VECTORINTRINSICS
    #include "MonsterMathSSE.h"
#else
    #include "MonsterMathFPU.h"
#endif

namespace MonsterEngine
{
namespace Math
{

// ============================================================================
// Alignment Constants
// ============================================================================

#if MR_PLATFORM_MATH_USE_AVX
    constexpr size_t SIMD_FLOAT_ALIGNMENT = 32;
    constexpr size_t SIMD_DOUBLE_ALIGNMENT = 32;
#else
    constexpr size_t SIMD_FLOAT_ALIGNMENT = 16;
    constexpr size_t SIMD_DOUBLE_ALIGNMENT = 16;
#endif

// ============================================================================
// VectorRegister Type Selection
// ============================================================================

/**
 * @brief Type trait to select the appropriate VectorRegister type for a given scalar type
 */
template<typename T>
struct TVectorRegisterType
{
    // Default to scalar operations for unsupported types
    using Type = T;
};

template<>
struct TVectorRegisterType<float>
{
    using Type = VectorRegister4Float;
};

template<>
struct TVectorRegisterType<double>
{
    using Type = VectorRegister4Double;
};

/**
 * @brief Persistent VectorRegister type for member variables
 * 
 * This type is used for storing VectorRegister values as class members,
 * ensuring proper alignment for SIMD operations.
 */
template<typename T>
struct TPersistentVectorRegisterType
{
    using Type = typename TVectorRegisterType<T>::Type;
};

// ============================================================================
// Alignment Helper for Transform Types
// ============================================================================

template<typename T>
struct TAlignOfTransform
{
    static constexpr size_t Value = 16;
};

template<>
struct TAlignOfTransform<float>
{
    static constexpr size_t Value = SIMD_FLOAT_ALIGNMENT;
};

template<>
struct TAlignOfTransform<double>
{
    static constexpr size_t Value = SIMD_DOUBLE_ALIGNMENT;
};

} // namespace Math
} // namespace MonsterEngine
