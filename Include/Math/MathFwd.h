// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file MathFwd.h
 * @brief Forward declarations and type aliases for math types
 * 
 * This file provides forward declarations for all templated math types
 * and defines convenient type aliases for common precision variants.
 * Following UE5's Large World Coordinates (LWC) pattern.
 */

#include <cstdint>
#include <type_traits>

// Define FORCEINLINE if not already defined
#ifndef FORCEINLINE
    #if defined(_MSC_VER)
        #define FORCEINLINE __forceinline
    #elif defined(__GNUC__) || defined(__clang__)
        #define FORCEINLINE inline __attribute__((always_inline))
    #else
        #define FORCEINLINE inline
    #endif
#endif

namespace MonsterEngine
{
namespace Math
{

// ============================================================================
// Forward Declarations for Template Math Types
// ============================================================================

template<typename T> struct TVector2;
template<typename T> struct TVector;
template<typename T> struct TVector4;
template<typename T> struct TQuat;
template<typename T> struct TRotator;
template<typename T> struct TMatrix;
template<typename T> struct TTransform;
template<typename T> struct TBox;
template<typename T> struct TSphere;
template<typename T> struct TPlane;

// ============================================================================
// Type Aliases - Default Precision (double for LWC support)
// ============================================================================

/** 2D Vector - default double precision */
using FVector2D     = TVector2<double>;
using FVector2f     = TVector2<float>;
using FVector2d     = TVector2<double>;

/** 3D Vector - default double precision */
using FVector       = TVector<double>;
using FVector3f     = TVector<float>;
using FVector3d     = TVector<double>;

/** 4D Vector - default double precision */
using FVector4      = TVector4<double>;
using FVector4f     = TVector4<float>;
using FVector4d     = TVector4<double>;

/** Quaternion - default double precision */
using FQuat         = TQuat<double>;
using FQuat4f       = TQuat<float>;
using FQuat4d       = TQuat<double>;

/** Rotator (Euler angles) - default double precision */
using FRotator      = TRotator<double>;
using FRotator3f    = TRotator<float>;
using FRotator3d    = TRotator<double>;

/** 4x4 Matrix - default double precision */
using FMatrix       = TMatrix<double>;
using FMatrix44f    = TMatrix<float>;
using FMatrix44d    = TMatrix<double>;

/** Transform (Translation + Rotation + Scale) - default double precision */
using FTransform    = TTransform<double>;
using FTransform3f  = TTransform<float>;
using FTransform3d  = TTransform<double>;

/** Axis-Aligned Bounding Box - default double precision */
using FBox          = TBox<double>;
using FBox3f        = TBox<float>;
using FBox3d        = TBox<double>;

/** Bounding Sphere - default double precision */
using FSphere       = TSphere<double>;
using FSphere3f     = TSphere<float>;
using FSphere3d     = TSphere<double>;

/** Plane - default double precision */
using FPlane        = TPlane<double>;
using FPlane4f      = TPlane<float>;
using FPlane4d      = TPlane<double>;

// ============================================================================
// Integer Point Types
// ============================================================================

/** 2D integer point */
struct FIntPoint
{
    int32_t X;
    int32_t Y;

    FIntPoint() : X(0), Y(0) {}
    FIntPoint(int32_t InX, int32_t InY) : X(InX), Y(InY) {}

    FORCEINLINE FIntPoint operator+(const FIntPoint& Other) const { return FIntPoint(X + Other.X, Y + Other.Y); }
    FORCEINLINE FIntPoint operator-(const FIntPoint& Other) const { return FIntPoint(X - Other.X, Y - Other.Y); }
    FORCEINLINE FIntPoint operator*(int32_t Scale) const { return FIntPoint(X * Scale, Y * Scale); }
    FORCEINLINE FIntPoint operator/(int32_t Divisor) const { return FIntPoint(X / Divisor, Y / Divisor); }

    FORCEINLINE FIntPoint& operator+=(const FIntPoint& Other) { X += Other.X; Y += Other.Y; return *this; }
    FORCEINLINE FIntPoint& operator-=(const FIntPoint& Other) { X -= Other.X; Y -= Other.Y; return *this; }

    FORCEINLINE bool operator==(const FIntPoint& Other) const { return X == Other.X && Y == Other.Y; }
    FORCEINLINE bool operator!=(const FIntPoint& Other) const { return X != Other.X || Y != Other.Y; }

    static const FIntPoint ZeroValue;
    static const FIntPoint NoneValue;
};

/** 3D integer vector */
struct FIntVector
{
    int32_t X;
    int32_t Y;
    int32_t Z;

    FIntVector() : X(0), Y(0), Z(0) {}
    FIntVector(int32_t InX, int32_t InY, int32_t InZ) : X(InX), Y(InY), Z(InZ) {}

    FORCEINLINE FIntVector operator+(const FIntVector& Other) const { return FIntVector(X + Other.X, Y + Other.Y, Z + Other.Z); }
    FORCEINLINE FIntVector operator-(const FIntVector& Other) const { return FIntVector(X - Other.X, Y - Other.Y, Z - Other.Z); }

    FORCEINLINE bool operator==(const FIntVector& Other) const { return X == Other.X && Y == Other.Y && Z == Other.Z; }
    FORCEINLINE bool operator!=(const FIntVector& Other) const { return X != Other.X || Y != Other.Y || Z != Other.Z; }

    static const FIntVector ZeroValue;
    static const FIntVector NoneValue;
};

// ============================================================================
// Type Traits for Math Types
// ============================================================================

/** Check if a type is a floating point type */
template<typename T>
struct TIsFloatingPoint : std::is_floating_point<T> {};

/** Check if a type is an integral type */
template<typename T>
struct TIsIntegral : std::is_integral<T> {};

/** Check if a type is arithmetic (integral or floating point) */
template<typename T>
struct TIsArithmetic : std::is_arithmetic<T> {};

// ============================================================================
// Force Initialization Enum
// ============================================================================

/** Enum used to force initialization of math types */
enum EForceInit
{
    ForceInit,
    ForceInitToZero
};

/** Enum used for no initialization (performance optimization) */
enum ENoInit
{
    NoInit
};

} // namespace Math

// ============================================================================
// Bring common types into MonsterEngine namespace for convenience
// ============================================================================

using Math::FVector2D;
using Math::FVector2f;
using Math::FVector2d;

using Math::FVector;
using Math::FVector3f;
using Math::FVector3d;

using Math::FVector4;
using Math::FVector4f;
using Math::FVector4d;

using Math::FQuat;
using Math::FQuat4f;
using Math::FQuat4d;

using Math::FRotator;
using Math::FRotator3f;
using Math::FRotator3d;

using Math::FMatrix;
using Math::FMatrix44f;
using Math::FMatrix44d;

using Math::FTransform;
using Math::FTransform3f;
using Math::FTransform3d;

using Math::FBox;
using Math::FBox3f;
using Math::FBox3d;

using Math::FSphere;
using Math::FSphere3f;
using Math::FSphere3d;

using Math::FPlane;
using Math::FPlane4f;
using Math::FPlane4d;

using Math::FIntPoint;
using Math::FIntVector;

using Math::EForceInit;
using Math::ForceInit;
using Math::ForceInitToZero;
using Math::ENoInit;
using Math::NoInit;

} // namespace MonsterEngine
