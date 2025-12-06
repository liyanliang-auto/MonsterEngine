// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file MonsterMathFPU.h
 * @brief Pure C++ FPU fallback implementation for platforms without SIMD support
 * 
 * This file provides scalar implementations of vector operations for platforms
 * that don't support SSE, AVX, or NEON instructions. All operations are
 * implemented using standard C++ math operations.
 */

#include "MathUtility.h"
#include <cmath>
#include <algorithm>

namespace MonsterEngine
{
namespace Math
{

// ============================================================================
// Alignment Constants for FPU (no special alignment required)
// ============================================================================

#define MR_FPU_FLOAT_ALIGNMENT  4
#define MR_FPU_DOUBLE_ALIGNMENT 8

// ============================================================================
// VectorRegister4Float - 4 floats stored as a struct
// ============================================================================

/**
 * @brief 4 single-precision floats for scalar FPU operations
 */
struct alignas(16) VectorRegister4Float
{
    float V[4];

    // Default constructor
    FORCEINLINE VectorRegister4Float() = default;

    // Construct from 4 floats
    FORCEINLINE VectorRegister4Float(float X, float Y, float Z, float W)
    {
        V[0] = X;
        V[1] = Y;
        V[2] = Z;
        V[3] = W;
    }

    // Construct from single value (broadcast)
    explicit FORCEINLINE VectorRegister4Float(float Value)
    {
        V[0] = V[1] = V[2] = V[3] = Value;
    }

    // Array access
    FORCEINLINE float& operator[](int Index) { return V[Index]; }
    FORCEINLINE float operator[](int Index) const { return V[Index]; }
};

// ============================================================================
// VectorRegister4Int - 4 32-bit integers stored as a struct
// ============================================================================

struct alignas(16) VectorRegister4Int
{
    int32_t V[4];

    FORCEINLINE VectorRegister4Int() = default;

    FORCEINLINE VectorRegister4Int(int32_t X, int32_t Y, int32_t Z, int32_t W)
    {
        V[0] = X;
        V[1] = Y;
        V[2] = Z;
        V[3] = W;
    }

    FORCEINLINE int32_t& operator[](int Index) { return V[Index]; }
    FORCEINLINE int32_t operator[](int Index) const { return V[Index]; }
};

// ============================================================================
// VectorRegister2Double - 2 doubles stored as a struct
// ============================================================================

struct alignas(16) VectorRegister2Double
{
    double V[2];

    FORCEINLINE VectorRegister2Double() = default;

    FORCEINLINE VectorRegister2Double(double X, double Y)
    {
        V[0] = X;
        V[1] = Y;
    }

    FORCEINLINE double& operator[](int Index) { return V[Index]; }
    FORCEINLINE double operator[](int Index) const { return V[Index]; }
};

// ============================================================================
// VectorRegister4Double - 4 doubles stored as a struct
// ============================================================================

/**
 * @brief 4 double-precision values for scalar FPU operations
 */
struct alignas(16) VectorRegister4Double
{
    double V[4];

    // Default constructor
    FORCEINLINE VectorRegister4Double() = default;

    // Construct from 4 doubles
    FORCEINLINE VectorRegister4Double(double X, double Y, double Z, double W)
    {
        V[0] = X;
        V[1] = Y;
        V[2] = Z;
        V[3] = W;
    }

    // Construct from single value (broadcast)
    explicit FORCEINLINE VectorRegister4Double(double Value)
    {
        V[0] = V[1] = V[2] = V[3] = Value;
    }

    // Construct from two 2-double registers
    FORCEINLINE VectorRegister4Double(const VectorRegister2Double& XY, const VectorRegister2Double& ZW)
    {
        V[0] = XY.V[0];
        V[1] = XY.V[1];
        V[2] = ZW.V[0];
        V[3] = ZW.V[1];
    }

    // Construct from 4-float register (conversion)
    FORCEINLINE VectorRegister4Double(const VectorRegister4Float& FloatVector)
    {
        V[0] = static_cast<double>(FloatVector.V[0]);
        V[1] = static_cast<double>(FloatVector.V[1]);
        V[2] = static_cast<double>(FloatVector.V[2]);
        V[3] = static_cast<double>(FloatVector.V[3]);
    }

    // Get XY as VectorRegister2Double
    FORCEINLINE VectorRegister2Double GetXY() const
    {
        return VectorRegister2Double(V[0], V[1]);
    }

    // Get ZW as VectorRegister2Double
    FORCEINLINE VectorRegister2Double GetZW() const
    {
        return VectorRegister2Double(V[2], V[3]);
    }

    // Array access
    FORCEINLINE double& operator[](int Index) { return V[Index]; }
    FORCEINLINE double operator[](int Index) const { return V[Index]; }
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
    return VectorRegister4Float(0.0f, 0.0f, 0.0f, 0.0f);
}

/** Zero vector register (double) */
FORCEINLINE VectorRegister4Double VectorZeroDouble()
{
    return VectorRegister4Double(0.0, 0.0, 0.0, 0.0);
}

/** One vector register (float) */
FORCEINLINE VectorRegister4Float VectorOneFloat()
{
    return VectorRegister4Float(1.0f, 1.0f, 1.0f, 1.0f);
}

/** One vector register (double) */
FORCEINLINE VectorRegister4Double VectorOneDouble()
{
    return VectorRegister4Double(1.0, 1.0, 1.0, 1.0);
}

// ============================================================================
// Load Operations
// ============================================================================

/** Load 4 floats from memory */
FORCEINLINE VectorRegister4Float VectorLoadAligned(const float* Ptr)
{
    return VectorRegister4Float(Ptr[0], Ptr[1], Ptr[2], Ptr[3]);
}

/** Load 4 floats from unaligned memory */
FORCEINLINE VectorRegister4Float VectorLoad(const float* Ptr)
{
    return VectorRegister4Float(Ptr[0], Ptr[1], Ptr[2], Ptr[3]);
}

/** Load 4 doubles from memory */
FORCEINLINE VectorRegister4Double VectorLoadAligned(const double* Ptr)
{
    return VectorRegister4Double(Ptr[0], Ptr[1], Ptr[2], Ptr[3]);
}

/** Load 4 doubles from unaligned memory */
FORCEINLINE VectorRegister4Double VectorLoad(const double* Ptr)
{
    return VectorRegister4Double(Ptr[0], Ptr[1], Ptr[2], Ptr[3]);
}

/** Create a vector from 4 float values */
FORCEINLINE VectorRegister4Float VectorSet(float X, float Y, float Z, float W)
{
    return VectorRegister4Float(X, Y, Z, W);
}

/** Create a vector from 4 double values */
FORCEINLINE VectorRegister4Double VectorSet(double X, double Y, double Z, double W)
{
    return VectorRegister4Double(X, Y, Z, W);
}

/** Replicate a single float to all 4 components */
FORCEINLINE VectorRegister4Float VectorSetFloat1(float Value)
{
    return VectorRegister4Float(Value);
}

/** Replicate a single double to all 4 components */
FORCEINLINE VectorRegister4Double VectorSetDouble1(double Value)
{
    return VectorRegister4Double(Value);
}

// ============================================================================
// Store Operations
// ============================================================================

/** Store 4 floats to memory */
FORCEINLINE void VectorStoreAligned(const VectorRegister4Float& Vec, float* Ptr)
{
    Ptr[0] = Vec.V[0];
    Ptr[1] = Vec.V[1];
    Ptr[2] = Vec.V[2];
    Ptr[3] = Vec.V[3];
}

/** Store 4 floats to unaligned memory */
FORCEINLINE void VectorStore(const VectorRegister4Float& Vec, float* Ptr)
{
    Ptr[0] = Vec.V[0];
    Ptr[1] = Vec.V[1];
    Ptr[2] = Vec.V[2];
    Ptr[3] = Vec.V[3];
}

/** Store 4 doubles to memory */
FORCEINLINE void VectorStoreAligned(const VectorRegister4Double& Vec, double* Ptr)
{
    Ptr[0] = Vec.V[0];
    Ptr[1] = Vec.V[1];
    Ptr[2] = Vec.V[2];
    Ptr[3] = Vec.V[3];
}

/** Store 4 doubles to unaligned memory */
FORCEINLINE void VectorStore(const VectorRegister4Double& Vec, double* Ptr)
{
    Ptr[0] = Vec.V[0];
    Ptr[1] = Vec.V[1];
    Ptr[2] = Vec.V[2];
    Ptr[3] = Vec.V[3];
}

// ============================================================================
// Arithmetic Operations - Float
// ============================================================================

/** Add two float vectors */
FORCEINLINE VectorRegister4Float VectorAdd(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
    return VectorRegister4Float(
        Vec1.V[0] + Vec2.V[0],
        Vec1.V[1] + Vec2.V[1],
        Vec1.V[2] + Vec2.V[2],
        Vec1.V[3] + Vec2.V[3]
    );
}

/** Subtract two float vectors */
FORCEINLINE VectorRegister4Float VectorSubtract(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
    return VectorRegister4Float(
        Vec1.V[0] - Vec2.V[0],
        Vec1.V[1] - Vec2.V[1],
        Vec1.V[2] - Vec2.V[2],
        Vec1.V[3] - Vec2.V[3]
    );
}

/** Multiply two float vectors */
FORCEINLINE VectorRegister4Float VectorMultiply(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
    return VectorRegister4Float(
        Vec1.V[0] * Vec2.V[0],
        Vec1.V[1] * Vec2.V[1],
        Vec1.V[2] * Vec2.V[2],
        Vec1.V[3] * Vec2.V[3]
    );
}

/** Divide two float vectors */
FORCEINLINE VectorRegister4Float VectorDivide(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
    return VectorRegister4Float(
        Vec1.V[0] / Vec2.V[0],
        Vec1.V[1] / Vec2.V[1],
        Vec1.V[2] / Vec2.V[2],
        Vec1.V[3] / Vec2.V[3]
    );
}

/** Negate a float vector */
FORCEINLINE VectorRegister4Float VectorNegate(const VectorRegister4Float& Vec)
{
    return VectorRegister4Float(-Vec.V[0], -Vec.V[1], -Vec.V[2], -Vec.V[3]);
}

// ============================================================================
// Arithmetic Operations - Double
// ============================================================================

/** Add two double vectors */
FORCEINLINE VectorRegister4Double VectorAdd(const VectorRegister4Double& Vec1, const VectorRegister4Double& Vec2)
{
    return VectorRegister4Double(
        Vec1.V[0] + Vec2.V[0],
        Vec1.V[1] + Vec2.V[1],
        Vec1.V[2] + Vec2.V[2],
        Vec1.V[3] + Vec2.V[3]
    );
}

/** Subtract two double vectors */
FORCEINLINE VectorRegister4Double VectorSubtract(const VectorRegister4Double& Vec1, const VectorRegister4Double& Vec2)
{
    return VectorRegister4Double(
        Vec1.V[0] - Vec2.V[0],
        Vec1.V[1] - Vec2.V[1],
        Vec1.V[2] - Vec2.V[2],
        Vec1.V[3] - Vec2.V[3]
    );
}

/** Multiply two double vectors */
FORCEINLINE VectorRegister4Double VectorMultiply(const VectorRegister4Double& Vec1, const VectorRegister4Double& Vec2)
{
    return VectorRegister4Double(
        Vec1.V[0] * Vec2.V[0],
        Vec1.V[1] * Vec2.V[1],
        Vec1.V[2] * Vec2.V[2],
        Vec1.V[3] * Vec2.V[3]
    );
}

/** Divide two double vectors */
FORCEINLINE VectorRegister4Double VectorDivide(const VectorRegister4Double& Vec1, const VectorRegister4Double& Vec2)
{
    return VectorRegister4Double(
        Vec1.V[0] / Vec2.V[0],
        Vec1.V[1] / Vec2.V[1],
        Vec1.V[2] / Vec2.V[2],
        Vec1.V[3] / Vec2.V[3]
    );
}

/** Negate a double vector */
FORCEINLINE VectorRegister4Double VectorNegate(const VectorRegister4Double& Vec)
{
    return VectorRegister4Double(-Vec.V[0], -Vec.V[1], -Vec.V[2], -Vec.V[3]);
}

// ============================================================================
// Math Operations - Float
// ============================================================================

/** Square root (float) */
FORCEINLINE VectorRegister4Float VectorSqrt(const VectorRegister4Float& Vec)
{
    return VectorRegister4Float(
        std::sqrt(Vec.V[0]),
        std::sqrt(Vec.V[1]),
        std::sqrt(Vec.V[2]),
        std::sqrt(Vec.V[3])
    );
}

/** Reciprocal square root (float) */
FORCEINLINE VectorRegister4Float VectorReciprocalSqrt(const VectorRegister4Float& Vec)
{
    return VectorRegister4Float(
        1.0f / std::sqrt(Vec.V[0]),
        1.0f / std::sqrt(Vec.V[1]),
        1.0f / std::sqrt(Vec.V[2]),
        1.0f / std::sqrt(Vec.V[3])
    );
}

/** Reciprocal (float) */
FORCEINLINE VectorRegister4Float VectorReciprocal(const VectorRegister4Float& Vec)
{
    return VectorRegister4Float(
        1.0f / Vec.V[0],
        1.0f / Vec.V[1],
        1.0f / Vec.V[2],
        1.0f / Vec.V[3]
    );
}

/** Minimum of two vectors (float) */
FORCEINLINE VectorRegister4Float VectorMin(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
    return VectorRegister4Float(
        std::min(Vec1.V[0], Vec2.V[0]),
        std::min(Vec1.V[1], Vec2.V[1]),
        std::min(Vec1.V[2], Vec2.V[2]),
        std::min(Vec1.V[3], Vec2.V[3])
    );
}

/** Maximum of two vectors (float) */
FORCEINLINE VectorRegister4Float VectorMax(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
    return VectorRegister4Float(
        std::max(Vec1.V[0], Vec2.V[0]),
        std::max(Vec1.V[1], Vec2.V[1]),
        std::max(Vec1.V[2], Vec2.V[2]),
        std::max(Vec1.V[3], Vec2.V[3])
    );
}

/** Absolute value (float) */
FORCEINLINE VectorRegister4Float VectorAbs(const VectorRegister4Float& Vec)
{
    return VectorRegister4Float(
        std::abs(Vec.V[0]),
        std::abs(Vec.V[1]),
        std::abs(Vec.V[2]),
        std::abs(Vec.V[3])
    );
}

// ============================================================================
// Math Operations - Double
// ============================================================================

/** Square root (double) */
FORCEINLINE VectorRegister4Double VectorSqrt(const VectorRegister4Double& Vec)
{
    return VectorRegister4Double(
        std::sqrt(Vec.V[0]),
        std::sqrt(Vec.V[1]),
        std::sqrt(Vec.V[2]),
        std::sqrt(Vec.V[3])
    );
}

/** Minimum of two vectors (double) */
FORCEINLINE VectorRegister4Double VectorMin(const VectorRegister4Double& Vec1, const VectorRegister4Double& Vec2)
{
    return VectorRegister4Double(
        std::min(Vec1.V[0], Vec2.V[0]),
        std::min(Vec1.V[1], Vec2.V[1]),
        std::min(Vec1.V[2], Vec2.V[2]),
        std::min(Vec1.V[3], Vec2.V[3])
    );
}

/** Maximum of two vectors (double) */
FORCEINLINE VectorRegister4Double VectorMax(const VectorRegister4Double& Vec1, const VectorRegister4Double& Vec2)
{
    return VectorRegister4Double(
        std::max(Vec1.V[0], Vec2.V[0]),
        std::max(Vec1.V[1], Vec2.V[1]),
        std::max(Vec1.V[2], Vec2.V[2]),
        std::max(Vec1.V[3], Vec2.V[3])
    );
}

/** Absolute value (double) */
FORCEINLINE VectorRegister4Double VectorAbs(const VectorRegister4Double& Vec)
{
    return VectorRegister4Double(
        std::abs(Vec.V[0]),
        std::abs(Vec.V[1]),
        std::abs(Vec.V[2]),
        std::abs(Vec.V[3])
    );
}

// ============================================================================
// Dot Product Operations
// ============================================================================

/** 3-component dot product (float), result in all components */
FORCEINLINE VectorRegister4Float VectorDot3(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
    float Dot = Vec1.V[0] * Vec2.V[0] + Vec1.V[1] * Vec2.V[1] + Vec1.V[2] * Vec2.V[2];
    return VectorRegister4Float(Dot, Dot, Dot, Dot);
}

/** 4-component dot product (float), result in all components */
FORCEINLINE VectorRegister4Float VectorDot4(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
    float Dot = Vec1.V[0] * Vec2.V[0] + Vec1.V[1] * Vec2.V[1] + Vec1.V[2] * Vec2.V[2] + Vec1.V[3] * Vec2.V[3];
    return VectorRegister4Float(Dot, Dot, Dot, Dot);
}

/** 3-component dot product returning scalar (float) */
FORCEINLINE float VectorDot3Scalar(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
    return Vec1.V[0] * Vec2.V[0] + Vec1.V[1] * Vec2.V[1] + Vec1.V[2] * Vec2.V[2];
}

/** 4-component dot product returning scalar (float) */
FORCEINLINE float VectorDot4Scalar(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
    return Vec1.V[0] * Vec2.V[0] + Vec1.V[1] * Vec2.V[1] + Vec1.V[2] * Vec2.V[2] + Vec1.V[3] * Vec2.V[3];
}

/** 3-component dot product (double), result in all components */
FORCEINLINE VectorRegister4Double VectorDot3(const VectorRegister4Double& Vec1, const VectorRegister4Double& Vec2)
{
    double Dot = Vec1.V[0] * Vec2.V[0] + Vec1.V[1] * Vec2.V[1] + Vec1.V[2] * Vec2.V[2];
    return VectorRegister4Double(Dot, Dot, Dot, Dot);
}

/** 4-component dot product (double), result in all components */
FORCEINLINE VectorRegister4Double VectorDot4(const VectorRegister4Double& Vec1, const VectorRegister4Double& Vec2)
{
    double Dot = Vec1.V[0] * Vec2.V[0] + Vec1.V[1] * Vec2.V[1] + Vec1.V[2] * Vec2.V[2] + Vec1.V[3] * Vec2.V[3];
    return VectorRegister4Double(Dot, Dot, Dot, Dot);
}

/** 3-component dot product returning scalar (double) */
FORCEINLINE double VectorDot3Scalar(const VectorRegister4Double& Vec1, const VectorRegister4Double& Vec2)
{
    return Vec1.V[0] * Vec2.V[0] + Vec1.V[1] * Vec2.V[1] + Vec1.V[2] * Vec2.V[2];
}

/** 4-component dot product returning scalar (double) */
FORCEINLINE double VectorDot4Scalar(const VectorRegister4Double& Vec1, const VectorRegister4Double& Vec2)
{
    return Vec1.V[0] * Vec2.V[0] + Vec1.V[1] * Vec2.V[1] + Vec1.V[2] * Vec2.V[2] + Vec1.V[3] * Vec2.V[3];
}

// ============================================================================
// Cross Product
// ============================================================================

/** 3-component cross product (float) */
FORCEINLINE VectorRegister4Float VectorCross(const VectorRegister4Float& Vec1, const VectorRegister4Float& Vec2)
{
    return VectorRegister4Float(
        Vec1.V[1] * Vec2.V[2] - Vec1.V[2] * Vec2.V[1],
        Vec1.V[2] * Vec2.V[0] - Vec1.V[0] * Vec2.V[2],
        Vec1.V[0] * Vec2.V[1] - Vec1.V[1] * Vec2.V[0],
        0.0f
    );
}

/** 3-component cross product (double) */
FORCEINLINE VectorRegister4Double VectorCross(const VectorRegister4Double& Vec1, const VectorRegister4Double& Vec2)
{
    return VectorRegister4Double(
        Vec1.V[1] * Vec2.V[2] - Vec1.V[2] * Vec2.V[1],
        Vec1.V[2] * Vec2.V[0] - Vec1.V[0] * Vec2.V[2],
        Vec1.V[0] * Vec2.V[1] - Vec1.V[1] * Vec2.V[0],
        0.0
    );
}

// ============================================================================
// Shuffle and Swizzle
// ============================================================================

/** Replicate X component to all lanes (float) */
FORCEINLINE VectorRegister4Float VectorReplicateX(const VectorRegister4Float& Vec)
{
    return VectorRegister4Float(Vec.V[0], Vec.V[0], Vec.V[0], Vec.V[0]);
}

/** Replicate Y component to all lanes (float) */
FORCEINLINE VectorRegister4Float VectorReplicateY(const VectorRegister4Float& Vec)
{
    return VectorRegister4Float(Vec.V[1], Vec.V[1], Vec.V[1], Vec.V[1]);
}

/** Replicate Z component to all lanes (float) */
FORCEINLINE VectorRegister4Float VectorReplicateZ(const VectorRegister4Float& Vec)
{
    return VectorRegister4Float(Vec.V[2], Vec.V[2], Vec.V[2], Vec.V[2]);
}

/** Replicate W component to all lanes (float) */
FORCEINLINE VectorRegister4Float VectorReplicateW(const VectorRegister4Float& Vec)
{
    return VectorRegister4Float(Vec.V[3], Vec.V[3], Vec.V[3], Vec.V[3]);
}

/** Replicate X component to all lanes (double) */
FORCEINLINE VectorRegister4Double VectorReplicateX(const VectorRegister4Double& Vec)
{
    return VectorRegister4Double(Vec.V[0], Vec.V[0], Vec.V[0], Vec.V[0]);
}

/** Replicate Y component to all lanes (double) */
FORCEINLINE VectorRegister4Double VectorReplicateY(const VectorRegister4Double& Vec)
{
    return VectorRegister4Double(Vec.V[1], Vec.V[1], Vec.V[1], Vec.V[1]);
}

/** Replicate Z component to all lanes (double) */
FORCEINLINE VectorRegister4Double VectorReplicateZ(const VectorRegister4Double& Vec)
{
    return VectorRegister4Double(Vec.V[2], Vec.V[2], Vec.V[2], Vec.V[2]);
}

/** Replicate W component to all lanes (double) */
FORCEINLINE VectorRegister4Double VectorReplicateW(const VectorRegister4Double& Vec)
{
    return VectorRegister4Double(Vec.V[3], Vec.V[3], Vec.V[3], Vec.V[3]);
}

} // namespace Math
} // namespace MonsterEngine
