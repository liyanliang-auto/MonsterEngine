// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file ProjectionMatrix.h
 * @brief Projection matrix generation functions
 * 
 * Provides functions to create perspective and orthographic projection matrices
 * with support for both standard and Reversed-Z depth conventions.
 * 
 * Reference: UE5 FReversedZPerspectiveMatrix, FReversedZOrthoMatrix
 */

#include "MathFwd.h"
#include "MathUtility.h"
#include "MathFunctions.h"
#include "Matrix.h"
#include "RHI/RHIDefinitions.h"
#include <cmath>

namespace MonsterEngine
{
namespace Math
{

/**
 * Create standard perspective projection matrix
 * Near plane maps to depth 0.0, Far plane maps to depth 1.0
 * 
 * This is the traditional projection matrix used in most graphics APIs.
 * However, it suffers from poor depth precision at far distances due to
 * non-linear depth distribution.
 * 
 * @param fovY Field of view in radians (vertical)
 * @param aspect Aspect ratio (width/height)
 * @param nearZ Near clipping plane distance (must be > 0)
 * @param farZ Far clipping plane distance (must be > nearZ)
 * @return 4x4 perspective projection matrix
 */
template<typename T>
inline TMatrix<T> PerspectiveStandard(T fovY, T aspect, T nearZ, T farZ)
{
    const T tanHalfFovy = FMath::Tan(fovY * T(0.5));
    
    TMatrix<T> result(ForceInit);
    
    // Standard perspective projection
    // Maps: Near → 0.0, Far → 1.0
    result.M[0][0] = T(1) / (aspect * tanHalfFovy);
    result.M[1][1] = T(1) / tanHalfFovy;
    result.M[2][2] = farZ / (farZ - nearZ);
    result.M[2][3] = T(1);
    result.M[3][2] = -(farZ * nearZ) / (farZ - nearZ);
    result.M[3][3] = T(0);
    
    return result;
}

/**
 * Create Reversed-Z perspective projection matrix
 * Near plane maps to depth 1.0, Far plane maps to depth 0.0
 * 
 * Benefits:
 * - Better floating-point precision distribution
 * - More precision at far distances (5-10x improvement)
 * - Reduces Z-fighting in large scenes
 * - Used by default in UE5, Unity, and other modern engines
 * 
 * The depth buffer is cleared to 0.0 (far plane) instead of 1.0.
 * Depth comparison function must be reversed (Less → Greater).
 * 
 * @param fovY Field of view in radians (vertical)
 * @param aspect Aspect ratio (width/height)
 * @param nearZ Near clipping plane distance (must be > 0)
 * @param farZ Far clipping plane distance (must be > nearZ)
 * @return 4x4 reversed-Z perspective projection matrix
 * 
 * Reference: UE5 FReversedZPerspectiveMatrix::FReversedZPerspectiveMatrix
 */
template<typename T>
inline TMatrix<T> PerspectiveReversedZ(T fovY, T aspect, T nearZ, T farZ)
{
    const T tanHalfFovy = FMath::Tan(fovY * T(0.5));
    
    TMatrix<T> result(ForceInit);
    
    // Reversed-Z perspective projection
    // Maps: Near → 1.0, Far → 0.0
    result.M[0][0] = T(1) / (aspect * tanHalfFovy);
    result.M[1][1] = T(1) / tanHalfFovy;
    result.M[2][2] = nearZ / (nearZ - farZ);  // Reversed!
    result.M[2][3] = T(1);
    result.M[3][2] = (farZ * nearZ) / (nearZ - farZ);  // No negative sign
    result.M[3][3] = T(0);
    
    return result;
}

/**
 * Create Infinite Reversed-Z perspective projection matrix
 * Near plane maps to depth 1.0, Far plane at infinity maps to depth 0.0
 * 
 * Benefits:
 * - No far plane clipping (infinite view distance)
 * - Maximum precision utilization
 * - Ideal for open-world games and large outdoor scenes
 * - Eliminates far plane artifacts
 * 
 * The far plane is effectively at infinity, so objects are never clipped
 * by the far plane. This is particularly useful for skyboxes and distant terrain.
 * 
 * @param fovY Field of view in radians (vertical)
 * @param aspect Aspect ratio (width/height)
 * @param nearZ Near clipping plane distance (must be > 0)
 * @return 4x4 infinite reversed-Z perspective projection matrix
 * 
 * Reference: UE5 FReversedZInfinitePerspectiveMatrix
 */
template<typename T>
inline TMatrix<T> PerspectiveInfiniteReversedZ(T fovY, T aspect, T nearZ)
{
    const T tanHalfFovy = FMath::Tan(fovY * T(0.5));
    
    TMatrix<T> result(ForceInit);
    
    // Infinite Reversed-Z perspective projection
    // Maps: Near → 1.0, Far (infinity) → 0.0
    result.M[0][0] = T(1) / (aspect * tanHalfFovy);
    result.M[1][1] = T(1) / tanHalfFovy;
    result.M[2][2] = T(0);  // Far = infinity → depth = 0
    result.M[2][3] = T(1);
    result.M[3][2] = nearZ;  // Near plane offset
    result.M[3][3] = T(0);
    
    return result;
}

/**
 * Create projection matrix based on depth range convention
 * Automatically selects the appropriate projection matrix type
 * 
 * @param fovY Field of view in radians (vertical)
 * @param aspect Aspect ratio (width/height)
 * @param nearZ Near clipping plane distance
 * @param farZ Far clipping plane distance (ignored for InfiniteReversed)
 * @param depthRange Depth range convention
 * @return 4x4 projection matrix
 */
template<typename T>
inline TMatrix<T> CreatePerspectiveMatrix(
    T fovY, 
    T aspect, 
    T nearZ, 
    T farZ,
    MonsterRender::RHI::EDepthRange depthRange = MonsterRender::RHI::EDepthRange::Standard)
{
    using namespace MonsterRender::RHI;
    
    switch (depthRange)
    {
        case EDepthRange::Standard:
            return PerspectiveStandard(fovY, aspect, nearZ, farZ);
            
        case EDepthRange::Reversed:
            return PerspectiveReversedZ(fovY, aspect, nearZ, farZ);
            
        case EDepthRange::InfiniteReversed:
            return PerspectiveInfiniteReversedZ(fovY, aspect, nearZ);
            
        default:
            return PerspectiveStandard(fovY, aspect, nearZ, farZ);
    }
}

/**
 * Create standard orthographic projection matrix
 * Near plane maps to depth 0.0, Far plane maps to depth 1.0
 * 
 * @param left Left clipping plane
 * @param right Right clipping plane
 * @param bottom Bottom clipping plane
 * @param top Top clipping plane
 * @param nearZ Near clipping plane
 * @param farZ Far clipping plane
 * @return 4x4 orthographic projection matrix
 */
template<typename T>
inline TMatrix<T> OrthographicStandard(T left, T right, T bottom, T top, T nearZ, T farZ)
{
    TMatrix<T> result(ForceInit);
    
    result.M[0][0] = T(2) / (right - left);
    result.M[1][1] = T(2) / (top - bottom);
    result.M[2][2] = T(1) / (farZ - nearZ);
    result.M[3][0] = -(right + left) / (right - left);
    result.M[3][1] = -(top + bottom) / (top - bottom);
    result.M[3][2] = -nearZ / (farZ - nearZ);
    result.M[3][3] = T(1);
    
    return result;
}

/**
 * Create Reversed-Z orthographic projection matrix
 * Near plane maps to depth 1.0, Far plane maps to depth 0.0
 * 
 * @param left Left clipping plane
 * @param right Right clipping plane
 * @param bottom Bottom clipping plane
 * @param top Top clipping plane
 * @param nearZ Near clipping plane
 * @param farZ Far clipping plane
 * @return 4x4 reversed-Z orthographic projection matrix
 */
template<typename T>
inline TMatrix<T> OrthographicReversedZ(T left, T right, T bottom, T top, T nearZ, T farZ)
{
    TMatrix<T> result(ForceInit);
    
    result.M[0][0] = T(2) / (right - left);
    result.M[1][1] = T(2) / (top - bottom);
    result.M[2][2] = T(1) / (nearZ - farZ);  // Reversed!
    result.M[3][0] = -(right + left) / (right - left);
    result.M[3][1] = -(top + bottom) / (top - bottom);
    result.M[3][2] = nearZ / (nearZ - farZ);  // Reversed!
    result.M[3][3] = T(1);
    
    return result;
}

} // namespace Math
} // namespace MonsterEngine
