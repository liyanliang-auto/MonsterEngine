// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file MonsterMath.h
 * @brief Unified entry header for the Monster Engine Math Library
 * 
 * This is the main header file for the Monster Engine math library.
 * Include this file to get access to all math types and functions.
 * 
 * The math library follows UE5's architecture with:
 * - Template-based precision support (float/double)
 * - SIMD transparent abstraction
 * - Memory alignment optimization
 * - NaN diagnostics in debug builds
 * 
 * Architecture Overview:
 * ┌─────────────────────────────────────────────────────────────────────┐
 * │                     MonsterMath.h (Unified Entry)                    │
 * │                   Includes all math type headers                     │
 * └─────────────────────────────────────────────────────────────────────┘
 *                                     │
 *         ┌───────────────────────────┼───────────────────────────┐
 *         ▼                           ▼                           ▼
 * ┌───────────────┐         ┌───────────────┐         ┌───────────────┐
 * │  Basic Types  │         │   Utilities   │         │ SIMD Platform │
 * │ TVector<T>    │         │    FMath      │         │ VectorRegister│
 * │ TQuat<T>      │         │ (static funcs)│         │ SSE/NEON/FPU  │
 * │ TMatrix<T>    │         │               │         │               │
 * │ TTransform<T> │         │               │         │               │
 * └───────────────┘         └───────────────┘         └───────────────┘
 * 
 * Type Aliases (default double precision for LWC support):
 * - FVector, FVector3f, FVector3d
 * - FQuat, FQuat4f, FQuat4d
 * - FRotator, FRotator3f, FRotator3d
 * - FMatrix, FMatrix44f, FMatrix44d
 * - FTransform, FTransform3f, FTransform3d
 * - FBox, FBox3f, FBox3d
 * - FSphere, FSphere3f, FSphere3d
 * - FPlane, FPlane4f, FPlane4d
 */

// ============================================================================
// Core Math Definitions
// ============================================================================

// Forward declarations and type aliases
#include "MathFwd.h"

// Math constants and utility macros
#include "MathUtility.h"

// ============================================================================
// SIMD Platform Abstraction
// ============================================================================

// Platform-specific SIMD types and operations
#include "VectorRegister.h"

// ============================================================================
// Basic Math Types
// ============================================================================

// 2D Vector
#include "Vector2D.h"

// 3D Vector
#include "Vector.h"

// 4D Vector
#include "Vector4.h"

// Quaternion (rotation)
#include "Quat.h"

// Rotator (Euler angles)
#include "Rotator.h"

// 4x4 Matrix
#include "Matrix.h"

// Transform (Translation + Rotation + Scale)
#include "Transform.h"

// ============================================================================
// Geometry Types
// ============================================================================

// Axis-Aligned Bounding Box
#include "Box.h"

// Bounding Sphere
#include "Sphere.h"

// Plane
#include "Plane.h"

// ============================================================================
// Math Utility Functions
// ============================================================================

// FMath static utility class
#include "MathFunctions.h"

// ============================================================================
// Convenience Namespace Aliases
// ============================================================================

namespace MonsterEngine
{

/**
 * @brief Convenience namespace for direct access to math types
 * 
 * Usage:
 *   using namespace MonsterEngine;
 *   FVector MyVector(1.0, 2.0, 3.0);
 *   FQuat MyRotation = FQuat::Identity;
 */

// All type aliases are already brought into MonsterEngine namespace via MathFwd.h

} // namespace MonsterEngine

// ============================================================================
// Global Operator Overloads
// ============================================================================

// Note: Non-member operators are defined in their respective header files

// ============================================================================
// Cross-Type Constructor Implementations
// These must be defined after all types are fully declared
// ============================================================================

namespace MonsterEngine
{
namespace Math
{

// TVector constructor from TVector4 (W is ignored)
template<typename T>
FORCEINLINE TVector<T>::TVector(const TVector4<T>& V)
    : X(V.X), Y(V.Y), Z(V.Z)
{
    DiagnosticCheckNaN();
}

// TVector constructor from TVector2 (Z = 0)
template<typename T>
FORCEINLINE TVector<T>::TVector(const TVector2<T>& V)
    : X(V.X), Y(V.Y), Z(T(0))
{
    DiagnosticCheckNaN();
}

} // namespace Math
} // namespace MonsterEngine

// ============================================================================
// Version Information
// ============================================================================

#define MONSTER_MATH_VERSION_MAJOR 1
#define MONSTER_MATH_VERSION_MINOR 0
#define MONSTER_MATH_VERSION_PATCH 0

#define MONSTER_MATH_VERSION_STRING "1.0.0"
