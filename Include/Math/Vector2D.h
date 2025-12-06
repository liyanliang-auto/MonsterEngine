// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file Vector2D.h
 * @brief 2D Vector template class
 * 
 * This file defines the TVector2<T> template class for 2D vector operations.
 * Supports both float and double precision following UE5's LWC pattern.
 */

#include "MathFwd.h"
#include "MathUtility.h"
#include <cmath>
#include <string>
#include <type_traits>

namespace MonsterEngine
{
namespace Math
{

/**
 * @brief A 2D vector template class
 * 
 * TVector2 represents a point or direction in 2D space.
 * The template parameter T must be a floating-point type (float or double).
 * 
 * @tparam T The scalar type (float or double)
 */
template<typename T>
struct TVector2
{
    static_assert(std::is_floating_point_v<T>, "TVector2 only supports float and double types.");

public:
    /** Type alias for the scalar type */
    using FReal = T;

    /** The vector's X component */
    T X;

    /** The vector's Y component */
    T Y;

public:
    // ========================================================================
    // Static Constants
    // ========================================================================

    /** A zero vector (0, 0) */
    static const TVector2<T> ZeroVector;

    /** A one vector (1, 1) */
    static const TVector2<T> OneVector;

    /** Unit X axis (1, 0) */
    static const TVector2<T> UnitX;

    /** Unit Y axis (0, 1) */
    static const TVector2<T> UnitY;

public:
    // ========================================================================
    // Constructors
    // ========================================================================

    /** Default constructor (no initialization for performance) */
    FORCEINLINE TVector2() {}

    /**
     * Constructor initializing all components to a single value
     * @param InF Value to set all components to
     */
    explicit FORCEINLINE TVector2(T InF)
        : X(InF), Y(InF)
    {
        DiagnosticCheckNaN();
    }

    /**
     * Constructor using initial values for each component
     * @param InX X component
     * @param InY Y component
     */
    FORCEINLINE TVector2(T InX, T InY)
        : X(InX), Y(InY)
    {
        DiagnosticCheckNaN();
    }

    /**
     * Constructor for force initialization
     * @param E Force init enum
     */
    explicit FORCEINLINE TVector2(EForceInit)
        : X(T(0)), Y(T(0))
    {
    }

    /**
     * Copy constructor from different precision
     * @param V Vector to copy from
     */
    template<typename FArg, typename = std::enable_if_t<!std::is_same_v<T, FArg>>>
    explicit FORCEINLINE TVector2(const TVector2<FArg>& V)
        : X(static_cast<T>(V.X))
        , Y(static_cast<T>(V.Y))
    {
        DiagnosticCheckNaN();
    }

    /**
     * Constructor from TVector (Z is ignored)
     * @param V 3D vector
     */
    explicit FORCEINLINE TVector2(const TVector<T>& V)
        : X(V.X), Y(V.Y)
    {
        DiagnosticCheckNaN();
    }

public:
    // ========================================================================
    // NaN Diagnostics
    // ========================================================================

#if ENABLE_NAN_DIAGNOSTIC
    FORCEINLINE void DiagnosticCheckNaN() const
    {
        if (ContainsNaN())
        {
            *const_cast<TVector2<T>*>(this) = ZeroVector;
        }
    }
#else
    FORCEINLINE void DiagnosticCheckNaN() const {}
#endif

    /** Check if any component is NaN */
    MR_NODISCARD FORCEINLINE bool ContainsNaN() const
    {
        return !std::isfinite(X) || !std::isfinite(Y);
    }

public:
    // ========================================================================
    // Operators
    // ========================================================================

    /** Get component by index (0=X, 1=Y) */
    MR_NODISCARD FORCEINLINE T& operator[](int32_t Index)
    {
        return (&X)[Index];
    }

    /** Get component by index (const) */
    MR_NODISCARD FORCEINLINE T operator[](int32_t Index) const
    {
        return (&X)[Index];
    }

    /** Component-wise addition */
    MR_NODISCARD FORCEINLINE TVector2<T> operator+(const TVector2<T>& V) const
    {
        return TVector2<T>(X + V.X, Y + V.Y);
    }

    /** Component-wise subtraction */
    MR_NODISCARD FORCEINLINE TVector2<T> operator-(const TVector2<T>& V) const
    {
        return TVector2<T>(X - V.X, Y - V.Y);
    }

    /** Component-wise multiplication */
    MR_NODISCARD FORCEINLINE TVector2<T> operator*(const TVector2<T>& V) const
    {
        return TVector2<T>(X * V.X, Y * V.Y);
    }

    /** Component-wise division */
    MR_NODISCARD FORCEINLINE TVector2<T> operator/(const TVector2<T>& V) const
    {
        return TVector2<T>(X / V.X, Y / V.Y);
    }

    /** Scalar multiplication */
    MR_NODISCARD FORCEINLINE TVector2<T> operator*(T Scale) const
    {
        return TVector2<T>(X * Scale, Y * Scale);
    }

    /** Scalar division */
    MR_NODISCARD FORCEINLINE TVector2<T> operator/(T Scale) const
    {
        const T RScale = T(1) / Scale;
        return TVector2<T>(X * RScale, Y * RScale);
    }

    /** Negation */
    MR_NODISCARD FORCEINLINE TVector2<T> operator-() const
    {
        return TVector2<T>(-X, -Y);
    }

    /** Addition assignment */
    FORCEINLINE TVector2<T>& operator+=(const TVector2<T>& V)
    {
        X += V.X; Y += V.Y;
        DiagnosticCheckNaN();
        return *this;
    }

    /** Subtraction assignment */
    FORCEINLINE TVector2<T>& operator-=(const TVector2<T>& V)
    {
        X -= V.X; Y -= V.Y;
        DiagnosticCheckNaN();
        return *this;
    }

    /** Multiplication assignment */
    FORCEINLINE TVector2<T>& operator*=(const TVector2<T>& V)
    {
        X *= V.X; Y *= V.Y;
        DiagnosticCheckNaN();
        return *this;
    }

    /** Division assignment */
    FORCEINLINE TVector2<T>& operator/=(const TVector2<T>& V)
    {
        X /= V.X; Y /= V.Y;
        DiagnosticCheckNaN();
        return *this;
    }

    /** Scalar multiplication assignment */
    FORCEINLINE TVector2<T>& operator*=(T Scale)
    {
        X *= Scale; Y *= Scale;
        DiagnosticCheckNaN();
        return *this;
    }

    /** Scalar division assignment */
    FORCEINLINE TVector2<T>& operator/=(T Scale)
    {
        const T RScale = T(1) / Scale;
        X *= RScale; Y *= RScale;
        DiagnosticCheckNaN();
        return *this;
    }

    /** Equality comparison */
    MR_NODISCARD FORCEINLINE bool operator==(const TVector2<T>& V) const
    {
        return X == V.X && Y == V.Y;
    }

    /** Inequality comparison */
    MR_NODISCARD FORCEINLINE bool operator!=(const TVector2<T>& V) const
    {
        return X != V.X || Y != V.Y;
    }

    /** Dot product using | operator */
    MR_NODISCARD FORCEINLINE T operator|(const TVector2<T>& V) const
    {
        return X * V.X + Y * V.Y;
    }

    /** Cross product (returns scalar - Z component of 3D cross) */
    MR_NODISCARD FORCEINLINE T operator^(const TVector2<T>& V) const
    {
        return X * V.Y - Y * V.X;
    }

public:
    // ========================================================================
    // Static Functions
    // ========================================================================

    /** Calculate the dot product of two vectors */
    MR_NODISCARD static FORCEINLINE T DotProduct(const TVector2<T>& A, const TVector2<T>& B)
    {
        return A | B;
    }

    /** Calculate the cross product (Z component of 3D cross) */
    MR_NODISCARD static FORCEINLINE T CrossProduct(const TVector2<T>& A, const TVector2<T>& B)
    {
        return A ^ B;
    }

    /** Calculate the distance between two points */
    MR_NODISCARD static FORCEINLINE T Distance(const TVector2<T>& V1, const TVector2<T>& V2)
    {
        return (V2 - V1).Size();
    }

    /** Calculate the squared distance between two points */
    MR_NODISCARD static FORCEINLINE T DistSquared(const TVector2<T>& V1, const TVector2<T>& V2)
    {
        return (V2 - V1).SizeSquared();
    }

public:
    // ========================================================================
    // Member Functions
    // ========================================================================

    /** Set all components */
    FORCEINLINE void Set(T InX, T InY)
    {
        X = InX;
        Y = InY;
        DiagnosticCheckNaN();
    }

    /** Get the length (magnitude) of this vector */
    MR_NODISCARD FORCEINLINE T Size() const
    {
        return std::sqrt(X * X + Y * Y);
    }

    /** Get the squared length of this vector */
    MR_NODISCARD FORCEINLINE T SizeSquared() const
    {
        return X * X + Y * Y;
    }

    /** Check if the vector is nearly zero */
    MR_NODISCARD FORCEINLINE bool IsNearlyZero(T Tolerance = MR_KINDA_SMALL_NUMBER) const
    {
        return std::abs(X) <= Tolerance && std::abs(Y) <= Tolerance;
    }

    /** Check if the vector is exactly zero */
    MR_NODISCARD FORCEINLINE bool IsZero() const
    {
        return X == T(0) && Y == T(0);
    }

    /** Check if this vector is unit length (normalized) */
    MR_NODISCARD FORCEINLINE bool IsNormalized() const
    {
        return std::abs(T(1) - SizeSquared()) < MR_THRESH_VECTOR_NORMALIZED;
    }

    /** Normalize this vector in-place */
    FORCEINLINE bool Normalize(T Tolerance = MR_SMALL_NUMBER)
    {
        const T SquareSum = SizeSquared();
        if (SquareSum > Tolerance)
        {
            const T Scale = T(1) / std::sqrt(SquareSum);
            X *= Scale;
            Y *= Scale;
            return true;
        }
        return false;
    }

    /** Get a normalized copy of this vector */
    MR_NODISCARD FORCEINLINE TVector2<T> GetNormalized() const
    {
        TVector2<T> Result = *this;
        Result.Normalize();
        return Result;
    }

    /**
     * Get a safely normalized copy of this vector
     * Returns zero vector if the vector is too small to normalize
     */
    MR_NODISCARD FORCEINLINE TVector2<T> GetSafeNormal(T Tolerance = MR_SMALL_NUMBER) const
    {
        const T SquareSum = SizeSquared();
        if (SquareSum == T(1))
        {
            return *this;
        }
        else if (SquareSum < Tolerance)
        {
            return ZeroVector;
        }
        const T Scale = T(1) / std::sqrt(SquareSum);
        return TVector2<T>(X * Scale, Y * Scale);
    }

    /** Get the absolute value of each component */
    MR_NODISCARD FORCEINLINE TVector2<T> GetAbs() const
    {
        return TVector2<T>(std::abs(X), std::abs(Y));
    }

    /** Get the maximum component value */
    MR_NODISCARD FORCEINLINE T GetMax() const
    {
        return std::max(X, Y);
    }

    /** Get the minimum component value */
    MR_NODISCARD FORCEINLINE T GetMin() const
    {
        return std::min(X, Y);
    }

    /** Get the maximum absolute component value */
    MR_NODISCARD FORCEINLINE T GetAbsMax() const
    {
        return std::max(std::abs(X), std::abs(Y));
    }

    /** Get the minimum absolute component value */
    MR_NODISCARD FORCEINLINE T GetAbsMin() const
    {
        return std::min(std::abs(X), std::abs(Y));
    }

    /** Component-wise min */
    MR_NODISCARD FORCEINLINE TVector2<T> ComponentMin(const TVector2<T>& Other) const
    {
        return TVector2<T>(std::min(X, Other.X), std::min(Y, Other.Y));
    }

    /** Component-wise max */
    MR_NODISCARD FORCEINLINE TVector2<T> ComponentMax(const TVector2<T>& Other) const
    {
        return TVector2<T>(std::max(X, Other.X), std::max(Y, Other.Y));
    }

    /** Check if vectors are equal within tolerance */
    MR_NODISCARD FORCEINLINE bool Equals(const TVector2<T>& V, T Tolerance = MR_KINDA_SMALL_NUMBER) const
    {
        return std::abs(X - V.X) <= Tolerance && std::abs(Y - V.Y) <= Tolerance;
    }

    /** Get a rotated copy of this vector */
    MR_NODISCARD FORCEINLINE TVector2<T> GetRotated(T AngleDeg) const
    {
        T S, C;
        const T AngleRad = AngleDeg * (MR_PI / T(180));
        S = std::sin(AngleRad);
        C = std::cos(AngleRad);
        return TVector2<T>(X * C - Y * S, X * S + Y * C);
    }

    /** Get a perpendicular vector (rotated 90 degrees counter-clockwise) */
    MR_NODISCARD FORCEINLINE TVector2<T> GetPerp() const
    {
        return TVector2<T>(-Y, X);
    }

    /** Convert to a string representation */
    MR_NODISCARD inline std::string ToString() const
    {
        char Buffer[64];
        snprintf(Buffer, sizeof(Buffer), "X=%.6f Y=%.6f", 
                 static_cast<double>(X), static_cast<double>(Y));
        return std::string(Buffer);
    }
};

// ============================================================================
// Static Constant Definitions
// ============================================================================

template<typename T> const TVector2<T> TVector2<T>::ZeroVector(T(0), T(0));
template<typename T> const TVector2<T> TVector2<T>::OneVector(T(1), T(1));
template<typename T> const TVector2<T> TVector2<T>::UnitX(T(1), T(0));
template<typename T> const TVector2<T> TVector2<T>::UnitY(T(0), T(1));

// ============================================================================
// Non-member Operators
// ============================================================================

/** Scalar * Vector */
template<typename T>
MR_NODISCARD FORCEINLINE TVector2<T> operator*(T Scale, const TVector2<T>& V)
{
    return V * Scale;
}

} // namespace Math
} // namespace MonsterEngine
