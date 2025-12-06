// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file Vector.h
 * @brief 3D Vector template class
 * 
 * This file defines the TVector<T> template class for 3D vector operations.
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
 * @brief A 3D vector template class
 * 
 * TVector represents a point or direction in 3D space.
 * The template parameter T must be a floating-point type (float or double).
 * 
 * @tparam T The scalar type (float or double)
 */
template<typename T>
struct TVector
{
    static_assert(std::is_floating_point_v<T>, "TVector only supports float and double types.");

public:
    /** Type alias for the scalar type */
    using FReal = T;

    /** The vector's X component */
    T X;

    /** The vector's Y component */
    T Y;

    /** The vector's Z component */
    T Z;

public:
    // ========================================================================
    // Static Constants
    // ========================================================================

    /** A zero vector (0, 0, 0) */
    static const TVector<T> ZeroVector;

    /** A one vector (1, 1, 1) */
    static const TVector<T> OneVector;

    /** Up vector (0, 0, 1) - Z is up in UE convention */
    static const TVector<T> UpVector;

    /** Down vector (0, 0, -1) */
    static const TVector<T> DownVector;

    /** Forward vector (1, 0, 0) - X is forward in UE convention */
    static const TVector<T> ForwardVector;

    /** Backward vector (-1, 0, 0) */
    static const TVector<T> BackwardVector;

    /** Right vector (0, 1, 0) - Y is right in UE convention */
    static const TVector<T> RightVector;

    /** Left vector (0, -1, 0) */
    static const TVector<T> LeftVector;

    /** Unit X axis (1, 0, 0) */
    static const TVector<T> XAxisVector;

    /** Unit Y axis (0, 1, 0) */
    static const TVector<T> YAxisVector;

    /** Unit Z axis (0, 0, 1) */
    static const TVector<T> ZAxisVector;

public:
    // ========================================================================
    // Constructors
    // ========================================================================

    /** Default constructor (no initialization for performance) */
    FORCEINLINE TVector() {}

    /**
     * Constructor initializing all components to a single value
     * @param InF Value to set all components to
     */
    explicit FORCEINLINE TVector(T InF)
        : X(InF), Y(InF), Z(InF)
    {
        DiagnosticCheckNaN();
    }

    /**
     * Constructor using initial values for each component
     * @param InX X component
     * @param InY Y component
     * @param InZ Z component
     */
    FORCEINLINE TVector(T InX, T InY, T InZ)
        : X(InX), Y(InY), Z(InZ)
    {
        DiagnosticCheckNaN();
    }

    /**
     * Constructor for force initialization
     * @param E Force init enum
     */
    explicit FORCEINLINE TVector(EForceInit)
        : X(T(0)), Y(T(0)), Z(T(0))
    {
    }

    /**
     * Copy constructor from different precision
     * @param V Vector to copy from
     */
    template<typename FArg, typename = std::enable_if_t<!std::is_same_v<T, FArg>>>
    explicit FORCEINLINE TVector(const TVector<FArg>& V)
        : X(static_cast<T>(V.X))
        , Y(static_cast<T>(V.Y))
        , Z(static_cast<T>(V.Z))
    {
        DiagnosticCheckNaN();
    }

    /**
     * Constructor from TVector2 (Z = 0)
     * @param V 2D vector
     */
    explicit FORCEINLINE TVector(const TVector2<T>& V);

    /**
     * Constructor from TVector4 (W is ignored)
     * @param V 4D vector
     */
    explicit FORCEINLINE TVector(const TVector4<T>& V);

public:
    // ========================================================================
    // NaN Diagnostics
    // ========================================================================

#if ENABLE_NAN_DIAGNOSTIC
    FORCEINLINE void DiagnosticCheckNaN() const
    {
        if (ContainsNaN())
        {
            // Log error and reset to zero
            *const_cast<TVector<T>*>(this) = ZeroVector;
        }
    }

    FORCEINLINE void DiagnosticCheckNaN(const char* Message) const
    {
        if (ContainsNaN())
        {
            *const_cast<TVector<T>*>(this) = ZeroVector;
        }
    }
#else
    FORCEINLINE void DiagnosticCheckNaN() const {}
    FORCEINLINE void DiagnosticCheckNaN(const char* Message) const {}
#endif

    /** Check if any component is NaN */
    MR_NODISCARD FORCEINLINE bool ContainsNaN() const
    {
        return !std::isfinite(X) || !std::isfinite(Y) || !std::isfinite(Z);
    }

public:
    // ========================================================================
    // Operators
    // ========================================================================

    /** Get component by index (0=X, 1=Y, 2=Z) */
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
    MR_NODISCARD FORCEINLINE TVector<T> operator+(const TVector<T>& V) const
    {
        return TVector<T>(X + V.X, Y + V.Y, Z + V.Z);
    }

    /** Component-wise subtraction */
    MR_NODISCARD FORCEINLINE TVector<T> operator-(const TVector<T>& V) const
    {
        return TVector<T>(X - V.X, Y - V.Y, Z - V.Z);
    }

    /** Component-wise multiplication */
    MR_NODISCARD FORCEINLINE TVector<T> operator*(const TVector<T>& V) const
    {
        return TVector<T>(X * V.X, Y * V.Y, Z * V.Z);
    }

    /** Component-wise division */
    MR_NODISCARD FORCEINLINE TVector<T> operator/(const TVector<T>& V) const
    {
        return TVector<T>(X / V.X, Y / V.Y, Z / V.Z);
    }

    /** Scalar multiplication */
    MR_NODISCARD FORCEINLINE TVector<T> operator*(T Scale) const
    {
        return TVector<T>(X * Scale, Y * Scale, Z * Scale);
    }

    /** Scalar division */
    MR_NODISCARD FORCEINLINE TVector<T> operator/(T Scale) const
    {
        const T RScale = T(1) / Scale;
        return TVector<T>(X * RScale, Y * RScale, Z * RScale);
    }

    /** Negation */
    MR_NODISCARD FORCEINLINE TVector<T> operator-() const
    {
        return TVector<T>(-X, -Y, -Z);
    }

    /** Addition assignment */
    FORCEINLINE TVector<T>& operator+=(const TVector<T>& V)
    {
        X += V.X; Y += V.Y; Z += V.Z;
        DiagnosticCheckNaN();
        return *this;
    }

    /** Subtraction assignment */
    FORCEINLINE TVector<T>& operator-=(const TVector<T>& V)
    {
        X -= V.X; Y -= V.Y; Z -= V.Z;
        DiagnosticCheckNaN();
        return *this;
    }

    /** Multiplication assignment */
    FORCEINLINE TVector<T>& operator*=(const TVector<T>& V)
    {
        X *= V.X; Y *= V.Y; Z *= V.Z;
        DiagnosticCheckNaN();
        return *this;
    }

    /** Division assignment */
    FORCEINLINE TVector<T>& operator/=(const TVector<T>& V)
    {
        X /= V.X; Y /= V.Y; Z /= V.Z;
        DiagnosticCheckNaN();
        return *this;
    }

    /** Scalar multiplication assignment */
    FORCEINLINE TVector<T>& operator*=(T Scale)
    {
        X *= Scale; Y *= Scale; Z *= Scale;
        DiagnosticCheckNaN();
        return *this;
    }

    /** Scalar division assignment */
    FORCEINLINE TVector<T>& operator/=(T Scale)
    {
        const T RScale = T(1) / Scale;
        X *= RScale; Y *= RScale; Z *= RScale;
        DiagnosticCheckNaN();
        return *this;
    }

    /** Equality comparison */
    MR_NODISCARD FORCEINLINE bool operator==(const TVector<T>& V) const
    {
        return X == V.X && Y == V.Y && Z == V.Z;
    }

    /** Inequality comparison */
    MR_NODISCARD FORCEINLINE bool operator!=(const TVector<T>& V) const
    {
        return X != V.X || Y != V.Y || Z != V.Z;
    }

    /** Dot product using | operator (UE5 convention) */
    MR_NODISCARD FORCEINLINE T operator|(const TVector<T>& V) const
    {
        return X * V.X + Y * V.Y + Z * V.Z;
    }

    /** Cross product using ^ operator (UE5 convention) */
    MR_NODISCARD FORCEINLINE TVector<T> operator^(const TVector<T>& V) const
    {
        return TVector<T>(
            Y * V.Z - Z * V.Y,
            Z * V.X - X * V.Z,
            X * V.Y - Y * V.X
        );
    }

public:
    // ========================================================================
    // Static Functions
    // ========================================================================

    /** Calculate the dot product of two vectors */
    MR_NODISCARD static FORCEINLINE T DotProduct(const TVector<T>& A, const TVector<T>& B)
    {
        return A | B;
    }

    /** Calculate the cross product of two vectors */
    MR_NODISCARD static FORCEINLINE TVector<T> CrossProduct(const TVector<T>& A, const TVector<T>& B)
    {
        return A ^ B;
    }

    /** Calculate the distance between two points */
    MR_NODISCARD static FORCEINLINE T Dist(const TVector<T>& V1, const TVector<T>& V2)
    {
        return (V2 - V1).Size();
    }

    /** Calculate the squared distance between two points */
    MR_NODISCARD static FORCEINLINE T DistSquared(const TVector<T>& V1, const TVector<T>& V2)
    {
        return (V2 - V1).SizeSquared();
    }

    /** Calculate the distance in the XY plane */
    MR_NODISCARD static FORCEINLINE T Dist2D(const TVector<T>& V1, const TVector<T>& V2)
    {
        return (V2 - V1).Size2D();
    }

    /** Calculate the squared distance in the XY plane */
    MR_NODISCARD static FORCEINLINE T DistSquared2D(const TVector<T>& V1, const TVector<T>& V2)
    {
        return (V2 - V1).SizeSquared2D();
    }

public:
    // ========================================================================
    // Member Functions
    // ========================================================================

    /** Set all components */
    FORCEINLINE void Set(T InX, T InY, T InZ)
    {
        X = InX;
        Y = InY;
        Z = InZ;
        DiagnosticCheckNaN();
    }

    /** Get the length (magnitude) of this vector */
    MR_NODISCARD FORCEINLINE T Size() const
    {
        return std::sqrt(X * X + Y * Y + Z * Z);
    }

    /** Get the squared length of this vector */
    MR_NODISCARD FORCEINLINE T SizeSquared() const
    {
        return X * X + Y * Y + Z * Z;
    }

    /** Get the length of the XY components */
    MR_NODISCARD FORCEINLINE T Size2D() const
    {
        return std::sqrt(X * X + Y * Y);
    }

    /** Get the squared length of the XY components */
    MR_NODISCARD FORCEINLINE T SizeSquared2D() const
    {
        return X * X + Y * Y;
    }

    /** Check if the vector is nearly zero */
    MR_NODISCARD FORCEINLINE bool IsNearlyZero(T Tolerance = MR_KINDA_SMALL_NUMBER) const
    {
        return std::abs(X) <= Tolerance && std::abs(Y) <= Tolerance && std::abs(Z) <= Tolerance;
    }

    /** Check if the vector is exactly zero */
    MR_NODISCARD FORCEINLINE bool IsZero() const
    {
        return X == T(0) && Y == T(0) && Z == T(0);
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
            Z *= Scale;
            return true;
        }
        return false;
    }

    /** Get a normalized copy of this vector */
    MR_NODISCARD FORCEINLINE TVector<T> GetNormalized() const
    {
        TVector<T> Result = *this;
        Result.Normalize();
        return Result;
    }

    /**
     * Get a safely normalized copy of this vector
     * Returns zero vector if the vector is too small to normalize
     */
    MR_NODISCARD FORCEINLINE TVector<T> GetSafeNormal(T Tolerance = MR_SMALL_NUMBER) const
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
        return TVector<T>(X * Scale, Y * Scale, Z * Scale);
    }

    /**
     * Get a safely normalized copy in the XY plane
     */
    MR_NODISCARD FORCEINLINE TVector<T> GetSafeNormal2D(T Tolerance = MR_SMALL_NUMBER) const
    {
        const T SquareSum = X * X + Y * Y;
        if (SquareSum == T(1))
        {
            if (Z == T(0))
            {
                return *this;
            }
            else
            {
                return TVector<T>(X, Y, T(0));
            }
        }
        else if (SquareSum < Tolerance)
        {
            return ZeroVector;
        }
        const T Scale = T(1) / std::sqrt(SquareSum);
        return TVector<T>(X * Scale, Y * Scale, T(0));
    }

    /** Get the absolute value of each component */
    MR_NODISCARD FORCEINLINE TVector<T> GetAbs() const
    {
        return TVector<T>(std::abs(X), std::abs(Y), std::abs(Z));
    }

    /** Get the maximum component value */
    MR_NODISCARD FORCEINLINE T GetMax() const
    {
        return std::max({ X, Y, Z });
    }

    /** Get the minimum component value */
    MR_NODISCARD FORCEINLINE T GetMin() const
    {
        return std::min({ X, Y, Z });
    }

    /** Get the maximum absolute component value */
    MR_NODISCARD FORCEINLINE T GetAbsMax() const
    {
        return std::max({ std::abs(X), std::abs(Y), std::abs(Z) });
    }

    /** Get the minimum absolute component value */
    MR_NODISCARD FORCEINLINE T GetAbsMin() const
    {
        return std::min({ std::abs(X), std::abs(Y), std::abs(Z) });
    }

    /** Get a vector with each component clamped to [Min, Max] */
    MR_NODISCARD FORCEINLINE TVector<T> GetClampedToSize(T Min, T Max) const
    {
        T VecSize = Size();
        if (VecSize < Min)
        {
            return GetSafeNormal() * Min;
        }
        else if (VecSize > Max)
        {
            return GetSafeNormal() * Max;
        }
        return *this;
    }

    /** Component-wise clamp */
    MR_NODISCARD FORCEINLINE TVector<T> ComponentMin(const TVector<T>& Other) const
    {
        return TVector<T>(
            std::min(X, Other.X),
            std::min(Y, Other.Y),
            std::min(Z, Other.Z)
        );
    }

    /** Component-wise max */
    MR_NODISCARD FORCEINLINE TVector<T> ComponentMax(const TVector<T>& Other) const
    {
        return TVector<T>(
            std::max(X, Other.X),
            std::max(Y, Other.Y),
            std::max(Z, Other.Z)
        );
    }

    /** Check if vectors are equal within tolerance */
    MR_NODISCARD FORCEINLINE bool Equals(const TVector<T>& V, T Tolerance = MR_KINDA_SMALL_NUMBER) const
    {
        return std::abs(X - V.X) <= Tolerance && 
               std::abs(Y - V.Y) <= Tolerance && 
               std::abs(Z - V.Z) <= Tolerance;
    }

    /** Check if all components are equal within tolerance */
    MR_NODISCARD FORCEINLINE bool AllComponentsEqual(T Tolerance = MR_KINDA_SMALL_NUMBER) const
    {
        return std::abs(X - Y) <= Tolerance && std::abs(X - Z) <= Tolerance;
    }

    /** Project this vector onto another vector */
    MR_NODISCARD FORCEINLINE TVector<T> ProjectOnTo(const TVector<T>& A) const
    {
        return A * (((*this) | A) / (A | A));
    }

    /** Project this vector onto a normal (assumes normal is unit length) */
    MR_NODISCARD FORCEINLINE TVector<T> ProjectOnToNormal(const TVector<T>& Normal) const
    {
        return Normal * ((*this) | Normal);
    }

    /** Reflect this vector about a normal */
    MR_NODISCARD FORCEINLINE TVector<T> MirrorByVector(const TVector<T>& MirrorNormal) const
    {
        return *this - MirrorNormal * (T(2) * ((*this) | MirrorNormal));
    }

    /** Rotate this vector around an axis */
    MR_NODISCARD TVector<T> RotateAngleAxis(T AngleDeg, const TVector<T>& Axis) const;

    /** Get the cosine of the angle between two vectors */
    MR_NODISCARD static FORCEINLINE T CosineAngle(const TVector<T>& A, const TVector<T>& B)
    {
        return (A | B) / std::sqrt(A.SizeSquared() * B.SizeSquared());
    }

    /** Convert to a string representation */
    MR_NODISCARD inline std::string ToString() const
    {
        char Buffer[128];
        snprintf(Buffer, sizeof(Buffer), "X=%.6f Y=%.6f Z=%.6f", 
                 static_cast<double>(X), static_cast<double>(Y), static_cast<double>(Z));
        return std::string(Buffer);
    }
};

// ============================================================================
// Static Constant Definitions
// ============================================================================

template<typename T> const TVector<T> TVector<T>::ZeroVector(T(0), T(0), T(0));
template<typename T> const TVector<T> TVector<T>::OneVector(T(1), T(1), T(1));
template<typename T> const TVector<T> TVector<T>::UpVector(T(0), T(0), T(1));
template<typename T> const TVector<T> TVector<T>::DownVector(T(0), T(0), T(-1));
template<typename T> const TVector<T> TVector<T>::ForwardVector(T(1), T(0), T(0));
template<typename T> const TVector<T> TVector<T>::BackwardVector(T(-1), T(0), T(0));
template<typename T> const TVector<T> TVector<T>::RightVector(T(0), T(1), T(0));
template<typename T> const TVector<T> TVector<T>::LeftVector(T(0), T(-1), T(0));
template<typename T> const TVector<T> TVector<T>::XAxisVector(T(1), T(0), T(0));
template<typename T> const TVector<T> TVector<T>::YAxisVector(T(0), T(1), T(0));
template<typename T> const TVector<T> TVector<T>::ZAxisVector(T(0), T(0), T(1));

// ============================================================================
// Non-member Operators
// ============================================================================

/** Scalar * Vector */
template<typename T>
MR_NODISCARD FORCEINLINE TVector<T> operator*(T Scale, const TVector<T>& V)
{
    return V * Scale;
}

} // namespace Math
} // namespace MonsterEngine
