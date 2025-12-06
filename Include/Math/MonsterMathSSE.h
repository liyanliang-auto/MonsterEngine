// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file MonsterMathSSE.h
 * @brief SSE/AVX SIMD implementation for x86/x64 platforms
 * 
 * This file provides SSE-based SIMD types and operations for vectorized math.
 * Supports SSE2 (baseline), SSE4.1, AVX, and AVX2 instruction sets.
 */

#include "MathUtility.h"

#if !defined(MR_PLATFORM_ENABLE_VECTORINTRINSICS) || !MR_PLATFORM_ENABLE_VECTORINTRINSICS
    #error "MonsterMathSSE.h should only be included on platforms with SSE support"
#endif

// Include SSE intrinsics
#include <emmintrin.h>  // SSE2

#if MR_PLATFORM_MATH_USE_SSE4_1
    #include <smmintrin.h>  // SSE4.1
#endif

#if MR_PLATFORM_MATH_USE_AVX
    #include <immintrin.h>  // AVX/AVX2
#endif

namespace MonsterEngine
{
namespace Math
{

// ============================================================================
// Alignment Constants for SSE
// ============================================================================

#define MR_SSE_FLOAT_ALIGNMENT  16

#if MR_PLATFORM_MATH_USE_AVX
    #define MR_SSE_DOUBLE_ALIGNMENT 32
#else
    #define MR_SSE_DOUBLE_ALIGNMENT 16
#endif

// ============================================================================
// Basic SIMD Types
// ============================================================================

/** 4 floats packed in a 128-bit register */
typedef __m128  VectorRegister4Float;

/** 4 32-bit integers packed in a 128-bit register */
typedef __m128i VectorRegister4Int;

/** 2 64-bit integers packed in a 128-bit register */
typedef __m128i VectorRegister2Int64;

/** 2 doubles packed in a 128-bit register */
typedef __m128d VectorRegister2Double;

// ============================================================================
// VectorRegister4Double - 4 doubles (requires 2x __m128d or 1x __m256d)
// ============================================================================

/**
 * @brief 4 double-precision values stored in SIMD registers
 * 
 * On AVX-capable systems, uses a single __m256d register.
 * On SSE-only systems, uses two __m128d registers.
 */
struct alignas(MR_SSE_DOUBLE_ALIGNMENT) VectorRegister4Double
{
#if !MR_PLATFORM_MATH_USE_AVX
    VectorRegister2Double XY;
    VectorRegister2Double ZW;

    FORCEINLINE VectorRegister2Double GetXY() const { return XY; }
    FORCEINLINE VectorRegister2Double GetZW() const { return ZW; }
#else
    union
    {
        struct
        {
            VectorRegister2Double XY;
            VectorRegister2Double ZW;
        };
        __m256d XYZW;
    };

    FORCEINLINE VectorRegister2Double GetXY() const { return _mm256_extractf128_pd(XYZW, 0); }
    FORCEINLINE VectorRegister2Double GetZW() const { return _mm256_extractf128_pd(XYZW, 1); }
#endif

    // Default constructor
    FORCEINLINE VectorRegister4Double() = default;

    // Construct from two 2-double registers
    FORCEINLINE VectorRegister4Double(const VectorRegister2Double& InXY, const VectorRegister2Double& InZW)
    {
#if MR_PLATFORM_MATH_USE_AVX
        XYZW = _mm256_setr_m128d(InXY, InZW);
#else
        XY = InXY;
        ZW = InZW;
#endif
    }

    // Construct from a 4-float register (conversion)
    FORCEINLINE VectorRegister4Double(const VectorRegister4Float& FloatVector)
    {
#if !MR_PLATFORM_MATH_USE_AVX
        XY = _mm_cvtps_pd(FloatVector);
        ZW = _mm_cvtps_pd(_mm_movehl_ps(FloatVector, FloatVector));
#else
        XYZW = _mm256_cvtps_pd(FloatVector);
#endif
    }

    // Assignment from 4-float register
    FORCEINLINE VectorRegister4Double& operator=(const VectorRegister4Float& FloatVector)
    {
#if !MR_PLATFORM_MATH_USE_AVX
        XY = _mm_cvtps_pd(FloatVector);
        ZW = _mm_cvtps_pd(_mm_movehl_ps(FloatVector, FloatVector));
#else
        XYZW = _mm256_cvtps_pd(FloatVector);
#endif
        return *this;
    }

#if MR_PLATFORM_MATH_USE_AVX
    // Construct from __m256d
    FORCEINLINE VectorRegister4Double(const __m256d& Register)
    {
        XYZW = Register;
    }

    // Assignment from __m256d
    FORCEINLINE VectorRegister4Double& operator=(const __m256d& Register)
    {
        XYZW = Register;
        return *this;
    }

    // Implicit conversion to __m256d
    FORCEINLINE operator __m256d() const
    {
        return XYZW;
    }
#endif
};

// ============================================================================
// Type Aliases
// ============================================================================

typedef VectorRegister4Int      VectorRegister4i;
typedef VectorRegister4Float    VectorRegister4f;
typedef VectorRegister4Double   VectorRegister4d;
typedef VectorRegister2Double   VectorRegister2d;

// Default VectorRegister is double precision for LWC support
typedef VectorRegister4Double   VectorRegister4;

// ============================================================================
// Global Constants
// ============================================================================

/** Zero vector register (float) */
FORCEINLINE VectorRegister4Float VectorZeroFloat()
{
    return _mm_setzero_ps();
}

/** Zero vector register (double) */
FORCEINLINE VectorRegister4Double VectorZeroDouble()
{
#if MR_PLATFORM_MATH_USE_AVX
    VectorRegister4Double Result;
    Result.XYZW = _mm256_setzero_pd();
    return Result;
#else
    return VectorRegister4Double(_mm_setzero_pd(), _mm_setzero_pd());
#endif
}

/** One vector register (float) */
FORCEINLINE VectorRegister4Float VectorOneFloat()
{
    return _mm_set1_ps(1.0f);
}

/** One vector register (double) */
FORCEINLINE VectorRegister4Double VectorOneDouble()
{
#if MR_PLATFORM_MATH_USE_AVX
    VectorRegister4Double Result;
    Result.XYZW = _mm256_set1_pd(1.0);
    return Result;
#else
    return VectorRegister4Double(_mm_set1_pd(1.0), _mm_set1_pd(1.0));
#endif
}

// ============================================================================
// Load Operations
// ============================================================================

/** Load 4 floats from aligned memory */
FORCEINLINE VectorRegister4Float VectorLoadAligned(const float* Ptr)
{
    return _mm_load_ps(Ptr);
}

/** Load 4 floats from unaligned memory */
FORCEINLINE VectorRegister4Float VectorLoad(const float* Ptr)
{
    return _mm_loadu_ps(Ptr);
}

/** Load 4 doubles from aligned memory */
FORCEINLINE VectorRegister4Double VectorLoadAligned(const double* Ptr)
{
#if MR_PLATFORM_MATH_USE_AVX
    VectorRegister4Double Result;
    Result.XYZW = _mm256_load_pd(Ptr);
    return Result;
#else
    return VectorRegister4Double(_mm_load_pd(Ptr), _mm_load_pd(Ptr + 2));
#endif
}

/** Load 4 doubles from unaligned memory */
FORCEINLINE VectorRegister4Double VectorLoad(const double* Ptr)
{
#if MR_PLATFORM_MATH_USE_AVX
    VectorRegister4Double Result;
    Result.XYZW = _mm256_loadu_pd(Ptr);
    return Result;
#else
    return VectorRegister4Double(_mm_loadu_pd(Ptr), _mm_loadu_pd(Ptr + 2));
#endif
}

/** Create a vector from 4 float values */
FORCEINLINE VectorRegister4Float VectorSet(float X, float Y, float Z, float W)
{
    return _mm_setr_ps(X, Y, Z, W);
}

/** Create a vector from 4 double values */
FORCEINLINE VectorRegister4Double VectorSet(double X, double Y, double Z, double W)
{
#if MR_PLATFORM_MATH_USE_AVX
    VectorRegister4Double Result;
    Result.XYZW = _mm256_setr_pd(X, Y, Z, W);
    return Result;
#else
    return VectorRegister4Double(_mm_setr_pd(X, Y), _mm_setr_pd(Z, W));
#endif
}

/** Replicate a single float to all 4 components */
FORCEINLINE VectorRegister4Float VectorSetFloat1(float Value)
{
    return _mm_set1_ps(Value);
}

/** Replicate a single double to all 4 components */
FORCEINLINE VectorRegister4Double VectorSetDouble1(double Value)
{
#if MR_PLATFORM_MATH_USE_AVX
    VectorRegister4Double Result;
    Result.XYZW = _mm256_set1_pd(Value);
    return Result;
#else
    return VectorRegister4Double(_mm_set1_pd(Value), _mm_set1_pd(Value));
#endif
}

// ============================================================================
// Store Operations
// ============================================================================

/** Store 4 floats to aligned memory */
FORCEINLINE void VectorStoreAligned(const VectorRegister4Float& Vec, float* Ptr)
{
    _mm_store_ps(Ptr, Vec);
}

/** Store 4 floats to unaligned memory */
FORCEINLINE void VectorStore(const VectorRegister4Float& Vec, float* Ptr)
{
    _mm_storeu_ps(Ptr, Vec);
}

/** Store 4 doubles to aligned memory */
FORCEINLINE void VectorStoreAligned(const VectorRegister4Double& Vec, double* Ptr)
{
#if MR_PLATFORM_MATH_USE_AVX
    _mm256_store_pd(Ptr, Vec.XYZW);
#else
    _mm_store_pd(Ptr, Vec.XY);
    _mm_store_pd(Ptr + 2, Vec.ZW);
#endif
}

/** Store 4 doubles to unaligned memory */
FORCEINLINE void VectorStore(const VectorRegister4Double& Vec, double* Ptr)
{
#if MR_PLATFORM_MATH_USE_AVX
    _mm256_storeu_pd(Ptr, Vec.XYZW);
#else
    _mm_storeu_pd(Ptr, Vec.XY);
    _mm_storeu_pd(Ptr + 2, Vec.ZW);
#endif
}

// ============================================================================
// Arithmetic Operations - Float
// ============================================================================

/** Add two float vectors */
FORCEINLINE VectorRegister4Float VectorAdd(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
    return _mm_add_ps(Vec1, Vec2);
}

/** Subtract two float vectors */
FORCEINLINE VectorRegister4Float VectorSubtract(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
    return _mm_sub_ps(Vec1, Vec2);
}

/** Multiply two float vectors */
FORCEINLINE VectorRegister4Float VectorMultiply(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
    return _mm_mul_ps(Vec1, Vec2);
}

/** Divide two float vectors */
FORCEINLINE VectorRegister4Float VectorDivide(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
    return _mm_div_ps(Vec1, Vec2);
}

/** Negate a float vector */
FORCEINLINE VectorRegister4Float VectorNegate(const VectorRegister4Float& Vec)
{
    return _mm_sub_ps(_mm_setzero_ps(), Vec);
}

// ============================================================================
// Arithmetic Operations - Double
// ============================================================================

/** Add two double vectors */
FORCEINLINE VectorRegister4Double VectorAdd(const VectorRegister4Double& Vec1, const VectorRegister4Double& Vec2)
{
#if MR_PLATFORM_MATH_USE_AVX
    VectorRegister4Double Result;
    Result.XYZW = _mm256_add_pd(Vec1.XYZW, Vec2.XYZW);
    return Result;
#else
    return VectorRegister4Double(
        _mm_add_pd(Vec1.XY, Vec2.XY),
        _mm_add_pd(Vec1.ZW, Vec2.ZW)
    );
#endif
}

/** Subtract two double vectors */
FORCEINLINE VectorRegister4Double VectorSubtract(const VectorRegister4Double& Vec1, const VectorRegister4Double& Vec2)
{
#if MR_PLATFORM_MATH_USE_AVX
    VectorRegister4Double Result;
    Result.XYZW = _mm256_sub_pd(Vec1.XYZW, Vec2.XYZW);
    return Result;
#else
    return VectorRegister4Double(
        _mm_sub_pd(Vec1.XY, Vec2.XY),
        _mm_sub_pd(Vec1.ZW, Vec2.ZW)
    );
#endif
}

/** Multiply two double vectors */
FORCEINLINE VectorRegister4Double VectorMultiply(const VectorRegister4Double& Vec1, const VectorRegister4Double& Vec2)
{
#if MR_PLATFORM_MATH_USE_AVX
    VectorRegister4Double Result;
    Result.XYZW = _mm256_mul_pd(Vec1.XYZW, Vec2.XYZW);
    return Result;
#else
    return VectorRegister4Double(
        _mm_mul_pd(Vec1.XY, Vec2.XY),
        _mm_mul_pd(Vec1.ZW, Vec2.ZW)
    );
#endif
}

/** Divide two double vectors */
FORCEINLINE VectorRegister4Double VectorDivide(const VectorRegister4Double& Vec1, const VectorRegister4Double& Vec2)
{
#if MR_PLATFORM_MATH_USE_AVX
    VectorRegister4Double Result;
    Result.XYZW = _mm256_div_pd(Vec1.XYZW, Vec2.XYZW);
    return Result;
#else
    return VectorRegister4Double(
        _mm_div_pd(Vec1.XY, Vec2.XY),
        _mm_div_pd(Vec1.ZW, Vec2.ZW)
    );
#endif
}

/** Negate a double vector */
FORCEINLINE VectorRegister4Double VectorNegate(const VectorRegister4Double& Vec)
{
#if MR_PLATFORM_MATH_USE_AVX
    VectorRegister4Double Result;
    Result.XYZW = _mm256_sub_pd(_mm256_setzero_pd(), Vec.XYZW);
    return Result;
#else
    return VectorRegister4Double(
        _mm_sub_pd(_mm_setzero_pd(), Vec.XY),
        _mm_sub_pd(_mm_setzero_pd(), Vec.ZW)
    );
#endif
}

// ============================================================================
// Comparison Operations - Float
// ============================================================================

/** Compare equal (float) */
FORCEINLINE VectorRegister4Float VectorCompareEQ(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
    return _mm_cmpeq_ps(Vec1, Vec2);
}

/** Compare not equal (float) */
FORCEINLINE VectorRegister4Float VectorCompareNE(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
    return _mm_cmpneq_ps(Vec1, Vec2);
}

/** Compare greater than (float) */
FORCEINLINE VectorRegister4Float VectorCompareGT(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
    return _mm_cmpgt_ps(Vec1, Vec2);
}

/** Compare greater than or equal (float) */
FORCEINLINE VectorRegister4Float VectorCompareGE(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
    return _mm_cmpge_ps(Vec1, Vec2);
}

/** Compare less than (float) */
FORCEINLINE VectorRegister4Float VectorCompareLT(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
    return _mm_cmplt_ps(Vec1, Vec2);
}

/** Compare less than or equal (float) */
FORCEINLINE VectorRegister4Float VectorCompareLE(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
    return _mm_cmple_ps(Vec1, Vec2);
}

// ============================================================================
// Math Operations - Float
// ============================================================================

/** Square root (float) */
FORCEINLINE VectorRegister4Float VectorSqrt(const VectorRegister4Float& Vec)
{
    return _mm_sqrt_ps(Vec);
}

/** Reciprocal square root estimate (float) */
FORCEINLINE VectorRegister4Float VectorReciprocalSqrt(const VectorRegister4Float& Vec)
{
    return _mm_rsqrt_ps(Vec);
}

/** Reciprocal estimate (float) */
FORCEINLINE VectorRegister4Float VectorReciprocal(const VectorRegister4Float& Vec)
{
    return _mm_rcp_ps(Vec);
}

/** Minimum of two vectors (float) */
FORCEINLINE VectorRegister4Float VectorMin(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
    return _mm_min_ps(Vec1, Vec2);
}

/** Maximum of two vectors (float) */
FORCEINLINE VectorRegister4Float VectorMax(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
    return _mm_max_ps(Vec1, Vec2);
}

/** Absolute value (float) */
FORCEINLINE VectorRegister4Float VectorAbs(const VectorRegister4Float& Vec)
{
    // Clear the sign bit
    static const __m128 SignMask = _mm_set1_ps(-0.0f);
    return _mm_andnot_ps(SignMask, Vec);
}

// ============================================================================
// Math Operations - Double
// ============================================================================

/** Square root (double) */
FORCEINLINE VectorRegister4Double VectorSqrt(const VectorRegister4Double& Vec)
{
#if MR_PLATFORM_MATH_USE_AVX
    VectorRegister4Double Result;
    Result.XYZW = _mm256_sqrt_pd(Vec.XYZW);
    return Result;
#else
    return VectorRegister4Double(
        _mm_sqrt_pd(Vec.XY),
        _mm_sqrt_pd(Vec.ZW)
    );
#endif
}

/** Minimum of two vectors (double) */
FORCEINLINE VectorRegister4Double VectorMin(const VectorRegister4Double& Vec1, const VectorRegister4Double& Vec2)
{
#if MR_PLATFORM_MATH_USE_AVX
    VectorRegister4Double Result;
    Result.XYZW = _mm256_min_pd(Vec1.XYZW, Vec2.XYZW);
    return Result;
#else
    return VectorRegister4Double(
        _mm_min_pd(Vec1.XY, Vec2.XY),
        _mm_min_pd(Vec1.ZW, Vec2.ZW)
    );
#endif
}

/** Maximum of two vectors (double) */
FORCEINLINE VectorRegister4Double VectorMax(const VectorRegister4Double& Vec1, const VectorRegister4Double& Vec2)
{
#if MR_PLATFORM_MATH_USE_AVX
    VectorRegister4Double Result;
    Result.XYZW = _mm256_max_pd(Vec1.XYZW, Vec2.XYZW);
    return Result;
#else
    return VectorRegister4Double(
        _mm_max_pd(Vec1.XY, Vec2.XY),
        _mm_max_pd(Vec1.ZW, Vec2.ZW)
    );
#endif
}

// ============================================================================
// Dot Product Operations
// ============================================================================

/** 3-component dot product (float), result in all components */
FORCEINLINE VectorRegister4Float VectorDot3(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
#if MR_PLATFORM_MATH_USE_SSE4_1
    return _mm_dp_ps(Vec1, Vec2, 0x7F);
#else
    VectorRegister4Float Temp = _mm_mul_ps(Vec1, Vec2);
    // Shuffle and add: X+Y, Z+W, X+Y, Z+W
    VectorRegister4Float Temp2 = _mm_shuffle_ps(Temp, Temp, _MM_SHUFFLE(2, 3, 0, 1));
    Temp = _mm_add_ps(Temp, Temp2);
    // Add X+Y+Z
    Temp2 = _mm_shuffle_ps(Temp, Temp, _MM_SHUFFLE(0, 1, 2, 3));
    return _mm_add_ps(Temp, Temp2);
#endif
}

/** 4-component dot product (float), result in all components */
FORCEINLINE VectorRegister4Float VectorDot4(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
#if MR_PLATFORM_MATH_USE_SSE4_1
    return _mm_dp_ps(Vec1, Vec2, 0xFF);
#else
    VectorRegister4Float Temp = _mm_mul_ps(Vec1, Vec2);
    VectorRegister4Float Temp2 = _mm_shuffle_ps(Temp, Temp, _MM_SHUFFLE(2, 3, 0, 1));
    Temp = _mm_add_ps(Temp, Temp2);
    Temp2 = _mm_shuffle_ps(Temp, Temp, _MM_SHUFFLE(1, 0, 3, 2));
    return _mm_add_ps(Temp, Temp2);
#endif
}

/** 3-component dot product returning scalar (float) */
FORCEINLINE float VectorDot3Scalar(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
    return _mm_cvtss_f32(VectorDot3(Vec1, Vec2));
}

/** 4-component dot product returning scalar (float) */
FORCEINLINE float VectorDot4Scalar(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
    return _mm_cvtss_f32(VectorDot4(Vec1, Vec2));
}

// ============================================================================
// Cross Product
// ============================================================================

/** 3-component cross product (float) */
FORCEINLINE VectorRegister4Float VectorCross(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
    // (Y1*Z2 - Z1*Y2, Z1*X2 - X1*Z2, X1*Y2 - Y1*X2, 0)
    VectorRegister4Float A_YZXW = _mm_shuffle_ps(Vec1, Vec1, _MM_SHUFFLE(3, 0, 2, 1));
    VectorRegister4Float B_ZXYW = _mm_shuffle_ps(Vec2, Vec2, _MM_SHUFFLE(3, 1, 0, 2));
    VectorRegister4Float A_ZXYW = _mm_shuffle_ps(Vec1, Vec1, _MM_SHUFFLE(3, 1, 0, 2));
    VectorRegister4Float B_YZXW = _mm_shuffle_ps(Vec2, Vec2, _MM_SHUFFLE(3, 0, 2, 1));
    
    return _mm_sub_ps(_mm_mul_ps(A_YZXW, B_ZXYW), _mm_mul_ps(A_ZXYW, B_YZXW));
}

// ============================================================================
// Shuffle and Swizzle
// ============================================================================

/** Replicate X component to all lanes (float) */
FORCEINLINE VectorRegister4Float VectorReplicateX(const VectorRegister4Float& Vec)
{
    return _mm_shuffle_ps(Vec, Vec, _MM_SHUFFLE(0, 0, 0, 0));
}

/** Replicate Y component to all lanes (float) */
FORCEINLINE VectorRegister4Float VectorReplicateY(const VectorRegister4Float& Vec)
{
    return _mm_shuffle_ps(Vec, Vec, _MM_SHUFFLE(1, 1, 1, 1));
}

/** Replicate Z component to all lanes (float) */
FORCEINLINE VectorRegister4Float VectorReplicateZ(const VectorRegister4Float& Vec)
{
    return _mm_shuffle_ps(Vec, Vec, _MM_SHUFFLE(2, 2, 2, 2));
}

/** Replicate W component to all lanes (float) */
FORCEINLINE VectorRegister4Float VectorReplicateW(const VectorRegister4Float& Vec)
{
    return _mm_shuffle_ps(Vec, Vec, _MM_SHUFFLE(3, 3, 3, 3));
}

// ============================================================================
// Bitwise Operations
// ============================================================================

/** Bitwise AND (float) */
FORCEINLINE VectorRegister4Float VectorBitwiseAnd(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
    return _mm_and_ps(Vec1, Vec2);
}

/** Bitwise OR (float) */
FORCEINLINE VectorRegister4Float VectorBitwiseOr(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
    return _mm_or_ps(Vec1, Vec2);
}

/** Bitwise XOR (float) */
FORCEINLINE VectorRegister4Float VectorBitwiseXor(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
    return _mm_xor_ps(Vec1, Vec2);
}

/** Select components based on mask (float) */
FORCEINLINE VectorRegister4Float VectorSelect(const VectorRegister4Float& Mask, const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
#if MR_PLATFORM_MATH_USE_SSE4_1
    return _mm_blendv_ps(Vec2, Vec1, Mask);
#else
    return _mm_or_ps(_mm_and_ps(Mask, Vec1), _mm_andnot_ps(Mask, Vec2));
#endif
}

} // namespace Math
} // namespace MonsterEngine
