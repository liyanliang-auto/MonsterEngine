// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file Vector4.h
 * @brief 4D Vector template class
 * 
 * This file defines the TVector4<T> template class for 4D vector operations.
 * Supports both float and double precision following UE5's LWC pattern.
 */

#include "MathFwd.h"
#include "MathUtility.h"
#include "VectorRegister.h"
#include <cmath>
#include <type_traits>

namespace MonsterEngine
{
namespace Math
{

/**
 * @brief A 4D vector template class
 * 
 * TVector4 represents a point or direction in 4D space, commonly used for
 * homogeneous coordinates in 3D graphics.
 * The template parameter T must be a floating-point type (float or double).
 * 
 * @tparam T The scalar type (float or double)
 */
template<typename T>
struct alignas(16) TVector4
{
    static_assert(std::is_floating_point_v<T>, "TVector4 only supports float and double types.");

public:
    /** Type alias for the scalar type */
    using FReal = T;

    /** The vector's X component */
    T X;

    /** The vector's Y component */
    T Y;

    /** The vector's Z component */
    T Z;

    /** The vector's W component */
    T W;

public:
    // ========================================================================
    // Static Constants
    // ========================================================================

    /** A zero vector (0, 0, 0, 0) */
    static const TVector4<T> ZeroVector;

    /** A one vector (1, 1, 1, 1) */
    static const TVector4<T> OneVector;

public:
    // ========================================================================
    // Constructors
    // ========================================================================

    /** Default constructor (no initialization for performance) */
    FORCEINLINE TVector4() {}

    /**
     * Constructor initializing all components to a single value
     * @param InF Value to set all components to
     */
    explicit FORCEINLINE TVector4(T InF)
        : X(InF), Y(InF), Z(InF), W(InF)
    {
        DiagnosticCheckNaN();
    }

    /**
     * Constructor using initial values for each component
     * @param InX X component
     * @param InY Y component
     * @param InZ Z component
     * @param InW W component
     */
    FORCEINLINE TVector4(T InX, T InY, T InZ, T InW)
        : X(InX), Y(InY), Z(InZ), W(InW)
    {
        DiagnosticCheckNaN();
    }

    /**
     * Constructor for force initialization
     * @param E Force init enum
     */
    explicit FORCEINLINE TVector4(EForceInit)
        : X(T(0)), Y(T(0)), Z(T(0)), W(T(0))
    {
    }

    /**
     * Constructor from TVector (W = 0 or 1)
     * @param V 3D vector
     * @param InW W component (default 0)
     */
    explicit FORCEINLINE TVector4(const TVector<T>& V, T InW = T(0))
        : X(V.X), Y(V.Y), Z(V.Z), W(InW)
    {
        DiagnosticCheckNaN();
    }

    /**
     * Copy constructor from different precision
     * @param V Vector to copy from
     */
    template<typename FArg, typename = std::enable_if_t<!std::is_same_v<T, FArg>>>
    explicit FORCEINLINE TVector4(const TVector4<FArg>& V)
        : X(static_cast<T>(V.X))
        , Y(static_cast<T>(V.Y))
        , Z(static_cast<T>(V.Z))
        , W(static_cast<T>(V.W))
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
            *const_cast<TVector4<T>*>(this) = ZeroVector;
        }
    }
#else
    FORCEINLINE void DiagnosticCheckNaN() const {}
#endif

    /** Check if any component is NaN */
    MR_NODISCARD FORCEINLINE bool ContainsNaN() const
    {
        return !std::isfinite(X) || !std::isfinite(Y) || !std::isfinite(Z) || !std::isfinite(W);
    }

public:
    // ========================================================================
    // Operators
    // ========================================================================

    /** Get component by index (0=X, 1=Y, 2=Z, 3=W) */
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
    MR_NODISCARD FORCEINLINE TVector4<T> operator+(const TVector4<T>& V) const
    {
        return TVector4<T>(X + V.X, Y + V.Y, Z + V.Z, W + V.W);
    }

    /** Component-wise subtraction */
    MR_NODISCARD FORCEINLINE TVector4<T> operator-(const TVector4<T>& V) const
    {
        return TVector4<T>(X - V.X, Y - V.Y, Z - V.Z, W - V.W);
    }

    /** Component-wise multiplication */
    MR_NODISCARD FORCEINLINE TVector4<T> operator*(const TVector4<T>& V) const
    {
        return TVector4<T>(X * V.X, Y * V.Y, Z * V.Z, W * V.W);
    }

    /** Component-wise division */
    MR_NODISCARD FORCEINLINE TVector4<T> operator/(const TVector4<T>& V) const
    {
        return TVector4<T>(X / V.X, Y / V.Y, Z / V.Z, W / V.W);
    }

    /** Scalar multiplication */
    MR_NODISCARD FORCEINLINE TVector4<T> operator*(T Scale) const
    {
        return TVector4<T>(X * Scale, Y * Scale, Z * Scale, W * Scale);
    }

    /** Scalar division */
    MR_NODISCARD FORCEINLINE TVector4<T> operator/(T Scale) const
    {
        const T RScale = T(1) / Scale;
        return TVector4<T>(X * RScale, Y * RScale, Z * RScale, W * RScale);
    }

    /** Negation */
    MR_NODISCARD FORCEINLINE TVector4<T> operator-() const
    {
        return TVector4<T>(-X, -Y, -Z, -W);
    }

    /** Addition assignment */
    FORCEINLINE TVector4<T>& operator+=(const TVector4<T>& V)
    {
        X += V.X; Y += V.Y; Z += V.Z; W += V.W;
        DiagnosticCheckNaN();
        return *this;
    }

    /** Subtraction assignment */
    FORCEINLINE TVector4<T>& operator-=(const TVector4<T>& V)
    {
        X -= V.X; Y -= V.Y; Z -= V.Z; W -= V.W;
        DiagnosticCheckNaN();
        return *this;
    }

    /** Multiplication assignment */
    FORCEINLINE TVector4<T>& operator*=(const TVector4<T>& V)
    {
        X *= V.X; Y *= V.Y; Z *= V.Z; W *= V.W;
        DiagnosticCheckNaN();
        return *this;
    }

    /** Division assignment */
    FORCEINLINE TVector4<T>& operator/=(const TVector4<T>& V)
    {
        X /= V.X; Y /= V.Y; Z /= V.Z; W /= V.W;
        DiagnosticCheckNaN();
        return *this;
    }

    /** Scalar multiplication assignment */
    FORCEINLINE TVector4<T>& operator*=(T Scale)
    {
        X *= Scale; Y *= Scale; Z *= Scale; W *= Scale;
        DiagnosticCheckNaN();
        return *this;
    }

    /** Scalar division assignment */
    FORCEINLINE TVector4<T>& operator/=(T Scale)
    {
        const T RScale = T(1) / Scale;
        X *= RScale; Y *= RScale; Z *= RScale; W *= RScale;
        DiagnosticCheckNaN();
        return *this;
    }

    /** Equality comparison */
    MR_NODISCARD FORCEINLINE bool operator==(const TVector4<T>& V) const
    {
        return X == V.X && Y == V.Y && Z == V.Z && W == V.W;
    }

    /** Inequality comparison */
    MR_NODISCARD FORCEINLINE bool operator!=(const TVector4<T>& V) const
    {
        return X != V.X || Y != V.Y || Z != V.Z || W != V.W;
    }

public:
    // ========================================================================
    // Static Functions
    // ========================================================================

    /** Calculate the 4-component dot product */
    MR_NODISCARD static FORCEINLINE T Dot4(const TVector4<T>& A, const TVector4<T>& B)
    {
        return A.X * B.X + A.Y * B.Y + A.Z * B.Z + A.W * B.W;
    }

    /** Calculate the 3-component dot product (ignoring W) */
    MR_NODISCARD static FORCEINLINE T Dot3(const TVector4<T>& A, const TVector4<T>& B)
    {
        return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
    }

public:
    // ========================================================================
    // Member Functions
    // ========================================================================

    /** Set all components */
    FORCEINLINE void Set(T InX, T InY, T InZ, T InW)
    {
        X = InX;
        Y = InY;
        Z = InZ;
        W = InW;
        DiagnosticCheckNaN();
    }

    /** Get the 3D length (ignoring W) */
    MR_NODISCARD FORCEINLINE T Size3() const
    {
        return std::sqrt(X * X + Y * Y + Z * Z);
    }

    /** Get the squared 3D length (ignoring W) */
    MR_NODISCARD FORCEINLINE T SizeSquared3() const
    {
        return X * X + Y * Y + Z * Z;
    }

    /** Get the 4D length */
    MR_NODISCARD FORCEINLINE T Size() const
    {
        return std::sqrt(X * X + Y * Y + Z * Z + W * W);
    }

    /** Get the squared 4D length */
    MR_NODISCARD FORCEINLINE T SizeSquared() const
    {
        return X * X + Y * Y + Z * Z + W * W;
    }

    /** Check if the vector is nearly zero */
    MR_NODISCARD FORCEINLINE bool IsNearlyZero3(T Tolerance = MR_KINDA_SMALL_NUMBER) const
    {
        return std::abs(X) <= Tolerance && std::abs(Y) <= Tolerance && std::abs(Z) <= Tolerance;
    }

    /** Check if the vector is exactly zero */
    MR_NODISCARD FORCEINLINE bool IsZero() const
    {
        return X == T(0) && Y == T(0) && Z == T(0) && W == T(0);
    }

    /** Check if this vector is unit length (normalized) in 3D */
    MR_NODISCARD FORCEINLINE bool IsNormalized3() const
    {
        return std::abs(T(1) - SizeSquared3()) < MR_THRESH_VECTOR_NORMALIZED;
    }

    /** Normalize the 3D components in-place (W unchanged) */
    FORCEINLINE bool Normalize3(T Tolerance = MR_SMALL_NUMBER)
    {
        const T SquareSum = SizeSquared3();
        if (SquareSum > Tolerance)
        {
            const T Scale = T(1) / std::sqrt(SquareSum);
            X *= Scale;
            Y *= Scale;
            Z *= Scale;
            return true;
        }
        return false;
    }

    /** Get a safely normalized copy (3D, W unchanged) */
    MR_NODISCARD FORCEINLINE TVector4<T> GetSafeNormal3(T Tolerance = MR_SMALL_NUMBER) const
    {
        const T SquareSum = SizeSquared3();
        if (SquareSum == T(1))
        {
            return *this;
        }
        else if (SquareSum < Tolerance)
        {
            return TVector4<T>(T(0), T(0), T(0), W);
        }
        const T Scale = T(1) / std::sqrt(SquareSum);
        return TVector4<T>(X * Scale, Y * Scale, Z * Scale, W);
    }

    /** Get the absolute value of each component */
    MR_NODISCARD FORCEINLINE TVector4<T> GetAbs() const
    {
        return TVector4<T>(std::abs(X), std::abs(Y), std::abs(Z), std::abs(W));
    }

    /** Get the maximum component value */
    MR_NODISCARD FORCEINLINE T GetMax() const
    {
        return std::max({ X, Y, Z, W });
    }

    /** Get the minimum component value */
    MR_NODISCARD FORCEINLINE T GetMin() const
    {
        return std::min({ X, Y, Z, W });
    }

    /** Component-wise min */
    MR_NODISCARD FORCEINLINE TVector4<T> ComponentMin(const TVector4<T>& Other) const
    {
        return TVector4<T>(
            std::min(X, Other.X),
            std::min(Y, Other.Y),
            std::min(Z, Other.Z),
            std::min(W, Other.W)
        );
    }

    /** Component-wise max */
    MR_NODISCARD FORCEINLINE TVector4<T> ComponentMax(const TVector4<T>& Other) const
    {
        return TVector4<T>(
            std::max(X, Other.X),
            std::max(Y, Other.Y),
            std::max(Z, Other.Z),
            std::max(W, Other.W)
        );
    }

    /** Check if vectors are equal within tolerance */
    MR_NODISCARD FORCEINLINE bool Equals(const TVector4<T>& V, T Tolerance = MR_KINDA_SMALL_NUMBER) const
    {
        return std::abs(X - V.X) <= Tolerance && 
               std::abs(Y - V.Y) <= Tolerance && 
               std::abs(Z - V.Z) <= Tolerance &&
               std::abs(W - V.W) <= Tolerance;
    }

    /** Check if 3D components are equal within tolerance */
    MR_NODISCARD FORCEINLINE bool Equals3(const TVector4<T>& V, T Tolerance = MR_KINDA_SMALL_NUMBER) const
    {
        return std::abs(X - V.X) <= Tolerance && 
               std::abs(Y - V.Y) <= Tolerance && 
               std::abs(Z - V.Z) <= Tolerance;
    }

    /** Convert to TVector (XYZ only) */
    MR_NODISCARD FORCEINLINE TVector<T> GetXYZ() const
    {
        return TVector<T>(X, Y, Z);
    }

    /** Convert to a string representation */
    MR_NODISCARD inline std::string ToString() const
    {
        char Buffer[128];
        snprintf(Buffer, sizeof(Buffer), "X=%.6f Y=%.6f Z=%.6f W=%.6f", 
                 static_cast<double>(X), static_cast<double>(Y), 
                 static_cast<double>(Z), static_cast<double>(W));
        return std::string(Buffer);
    }
};

// ============================================================================
// Static Constant Definitions
// ============================================================================

template<typename T> const TVector4<T> TVector4<T>::ZeroVector(T(0), T(0), T(0), T(0));
template<typename T> const TVector4<T> TVector4<T>::OneVector(T(1), T(1), T(1), T(1));

// ============================================================================
// Non-member Operators
// ============================================================================

/** Scalar * Vector */
template<typename T>
MR_NODISCARD FORCEINLINE TVector4<T> operator*(T Scale, const TVector4<T>& V)
{
    return V * Scale;
}

// ============================================================================
// TVector Constructor from TVector4
// ============================================================================

template<typename T>
FORCEINLINE TVector<T>::TVector(const TVector4<T>& V)
    : X(V.X), Y(V.Y), Z(V.Z)
{
    DiagnosticCheckNaN();
}

// ============================================================================
// TVector Constructor from TVector2
// ============================================================================

template<typename T>
FORCEINLINE TVector<T>::TVector(const TVector2<T>& V)
    : X(V.X), Y(V.Y), Z(T(0))
{
    DiagnosticCheckNaN();
}

} // namespace Math
} // namespace MonsterEngine
