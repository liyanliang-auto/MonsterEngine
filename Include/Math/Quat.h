// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file Quat.h
 * @brief Quaternion template class for rotation representation
 * 
 * This file defines the TQuat<T> template class for quaternion-based rotations.
 * Quaternions provide gimbal-lock-free rotation representation and smooth interpolation.
 * Supports both float and double precision following UE5's LWC pattern.
 */

#include "MathFwd.h"
#include "MathUtility.h"
#include "Vector.h"
#include <cmath>
#include <type_traits>

namespace MonsterEngine
{
namespace Math
{

// Forward declarations
template<typename T> struct TRotator;
template<typename T> struct TMatrix;

/**
 * @brief A quaternion template class for rotation representation
 * 
 * TQuat represents a rotation in 3D space using quaternion mathematics.
 * Quaternions avoid gimbal lock and provide smooth interpolation (Slerp).
 * 
 * Order matters when composing quaternions: C = A * B will yield a quaternion C
 * that logically first applies B then A to any subsequent transformation
 * (right first, then left).
 * 
 * @tparam T The scalar type (float or double)
 */
template<typename T>
struct alignas(16) TQuat
{
    static_assert(std::is_floating_point_v<T>, "TQuat only supports float and double types.");

public:
    /** Type alias for the scalar type */
    using FReal = T;

    /** The quaternion's X component (imaginary i) */
    T X;

    /** The quaternion's Y component (imaginary j) */
    T Y;

    /** The quaternion's Z component (imaginary k) */
    T Z;

    /** The quaternion's W component (real/scalar part) */
    T W;

public:
    // ========================================================================
    // Static Constants
    // ========================================================================

    /** Identity quaternion (no rotation) */
    static const TQuat<T> Identity;

public:
    // ========================================================================
    // Constructors
    // ========================================================================

    /** Default constructor (no initialization for performance) */
    FORCEINLINE TQuat() {}

    /**
     * Constructor with explicit components
     * @param InX X component
     * @param InY Y component
     * @param InZ Z component
     * @param InW W component
     */
    FORCEINLINE TQuat(T InX, T InY, T InZ, T InW)
        : X(InX), Y(InY), Z(InZ), W(InW)
    {
        DiagnosticCheckNaN();
    }

    /**
     * Constructor for force initialization
     * @param E Force init enum
     */
    explicit FORCEINLINE TQuat(EForceInit E)
        : X(T(0)), Y(T(0)), Z(T(0)), W(E == ForceInitToZero ? T(0) : T(1))
    {
    }

    /**
     * Constructor from axis and angle
     * @param Axis The axis of rotation (must be normalized)
     * @param AngleRad The angle of rotation in radians
     */
    FORCEINLINE TQuat(const TVector<T>& Axis, T AngleRad)
    {
        const T HalfAngle = AngleRad * T(0.5);
        const T S = std::sin(HalfAngle);
        const T C = std::cos(HalfAngle);

        X = S * Axis.X;
        Y = S * Axis.Y;
        Z = S * Axis.Z;
        W = C;

        DiagnosticCheckNaN();
    }

    /**
     * Copy constructor from different precision
     * @param Q Quaternion to copy from
     */
    template<typename FArg, typename = std::enable_if_t<!std::is_same_v<T, FArg>>>
    explicit FORCEINLINE TQuat(const TQuat<FArg>& Q)
        : X(static_cast<T>(Q.X))
        , Y(static_cast<T>(Q.Y))
        , Z(static_cast<T>(Q.Z))
        , W(static_cast<T>(Q.W))
    {
        DiagnosticCheckNaN();
    }

    /**
     * Constructor from rotator (Euler angles)
     * @param R The rotator to convert from
     */
    explicit TQuat(const TRotator<T>& R);

    /**
     * Constructor from rotation matrix
     * @param M The rotation matrix to convert from
     */
    explicit TQuat(const TMatrix<T>& M);

public:
    // ========================================================================
    // NaN Diagnostics
    // ========================================================================

#if ENABLE_NAN_DIAGNOSTIC
    FORCEINLINE void DiagnosticCheckNaN() const
    {
        if (ContainsNaN())
        {
            *const_cast<TQuat<T>*>(this) = Identity;
        }
    }

    FORCEINLINE void DiagnosticCheckNaN(const char* Message) const
    {
        if (ContainsNaN())
        {
            *const_cast<TQuat<T>*>(this) = Identity;
        }
    }
#else
    FORCEINLINE void DiagnosticCheckNaN() const {}
    FORCEINLINE void DiagnosticCheckNaN(const char* Message) const {}
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

    /**
     * Quaternion multiplication (composition)
     * Result = this * Q means: first apply Q, then apply this
     */
    MR_NODISCARD FORCEINLINE TQuat<T> operator*(const TQuat<T>& Q) const
    {
        return TQuat<T>(
            W * Q.X + X * Q.W + Y * Q.Z - Z * Q.Y,
            W * Q.Y - X * Q.Z + Y * Q.W + Z * Q.X,
            W * Q.Z + X * Q.Y - Y * Q.X + Z * Q.W,
            W * Q.W - X * Q.X - Y * Q.Y - Z * Q.Z
        );
    }

    /** Quaternion multiplication assignment */
    FORCEINLINE TQuat<T>& operator*=(const TQuat<T>& Q)
    {
        *this = *this * Q;
        DiagnosticCheckNaN();
        return *this;
    }

    /**
     * Rotate a vector by this quaternion
     * @param V The vector to rotate
     * @return The rotated vector
     */
    MR_NODISCARD FORCEINLINE TVector<T> operator*(const TVector<T>& V) const
    {
        return RotateVector(V);
    }

    /** Component-wise addition (not a rotation composition!) */
    MR_NODISCARD FORCEINLINE TQuat<T> operator+(const TQuat<T>& Q) const
    {
        return TQuat<T>(X + Q.X, Y + Q.Y, Z + Q.Z, W + Q.W);
    }

    /** Component-wise addition assignment */
    FORCEINLINE TQuat<T>& operator+=(const TQuat<T>& Q)
    {
        X += Q.X; Y += Q.Y; Z += Q.Z; W += Q.W;
        DiagnosticCheckNaN();
        return *this;
    }

    /** Component-wise subtraction */
    MR_NODISCARD FORCEINLINE TQuat<T> operator-(const TQuat<T>& Q) const
    {
        return TQuat<T>(X - Q.X, Y - Q.Y, Z - Q.Z, W - Q.W);
    }

    /** Component-wise subtraction assignment */
    FORCEINLINE TQuat<T>& operator-=(const TQuat<T>& Q)
    {
        X -= Q.X; Y -= Q.Y; Z -= Q.Z; W -= Q.W;
        DiagnosticCheckNaN();
        return *this;
    }

    /** Negation (represents the same rotation) */
    MR_NODISCARD FORCEINLINE TQuat<T> operator-() const
    {
        return TQuat<T>(-X, -Y, -Z, -W);
    }

    /** Scalar multiplication */
    MR_NODISCARD FORCEINLINE TQuat<T> operator*(T Scale) const
    {
        return TQuat<T>(X * Scale, Y * Scale, Z * Scale, W * Scale);
    }

    /** Scalar multiplication assignment */
    FORCEINLINE TQuat<T>& operator*=(T Scale)
    {
        X *= Scale; Y *= Scale; Z *= Scale; W *= Scale;
        DiagnosticCheckNaN();
        return *this;
    }

    /** Scalar division */
    MR_NODISCARD FORCEINLINE TQuat<T> operator/(T Scale) const
    {
        const T Inv = T(1) / Scale;
        return TQuat<T>(X * Inv, Y * Inv, Z * Inv, W * Inv);
    }

    /** Scalar division assignment */
    FORCEINLINE TQuat<T>& operator/=(T Scale)
    {
        const T Inv = T(1) / Scale;
        X *= Inv; Y *= Inv; Z *= Inv; W *= Inv;
        DiagnosticCheckNaN();
        return *this;
    }

    /** Equality comparison */
    MR_NODISCARD FORCEINLINE bool operator==(const TQuat<T>& Q) const
    {
        return X == Q.X && Y == Q.Y && Z == Q.Z && W == Q.W;
    }

    /** Inequality comparison */
    MR_NODISCARD FORCEINLINE bool operator!=(const TQuat<T>& Q) const
    {
        return X != Q.X || Y != Q.Y || Z != Q.Z || W != Q.W;
    }

    /** Dot product using | operator */
    MR_NODISCARD FORCEINLINE T operator|(const TQuat<T>& Q) const
    {
        return X * Q.X + Y * Q.Y + Z * Q.Z + W * Q.W;
    }

public:
    // ========================================================================
    // Static Functions
    // ========================================================================

    /**
     * Create a quaternion from axis and angle
     * @param Axis The axis of rotation (will be normalized)
     * @param AngleRad The angle of rotation in radians
     */
    MR_NODISCARD static FORCEINLINE TQuat<T> MakeFromAxisAngle(const TVector<T>& Axis, T AngleRad)
    {
        return TQuat<T>(Axis.GetSafeNormal(), AngleRad);
    }

    /**
     * Create a quaternion that rotates from one direction to another
     * @param From The starting direction (will be normalized)
     * @param To The target direction (will be normalized)
     */
    MR_NODISCARD static TQuat<T> FindBetween(const TVector<T>& From, const TVector<T>& To)
    {
        const TVector<T> FromNorm = From.GetSafeNormal();
        const TVector<T> ToNorm = To.GetSafeNormal();

        const T Dot = TVector<T>::DotProduct(FromNorm, ToNorm);

        if (Dot >= T(1) - MR_SMALL_NUMBER)
        {
            // Vectors are parallel and in the same direction
            return Identity;
        }
        else if (Dot <= T(-1) + MR_SMALL_NUMBER)
        {
            // Vectors are parallel and in opposite directions
            // Find an orthogonal axis
            TVector<T> Axis = TVector<T>::CrossProduct(TVector<T>::XAxisVector, FromNorm);
            if (Axis.SizeSquared() < MR_SMALL_NUMBER)
            {
                Axis = TVector<T>::CrossProduct(TVector<T>::YAxisVector, FromNorm);
            }
            return TQuat<T>(Axis.GetSafeNormal(), MR_PI);
        }
        else
        {
            const TVector<T> Cross = TVector<T>::CrossProduct(FromNorm, ToNorm);
            const T S = std::sqrt((T(1) + Dot) * T(2));
            const T InvS = T(1) / S;

            return TQuat<T>(
                Cross.X * InvS,
                Cross.Y * InvS,
                Cross.Z * InvS,
                S * T(0.5)
            );
        }
    }

    /**
     * Spherical linear interpolation between two quaternions
     * @param A The starting quaternion
     * @param B The ending quaternion
     * @param Alpha The interpolation factor [0, 1]
     */
    MR_NODISCARD static TQuat<T> Slerp(const TQuat<T>& A, const TQuat<T>& B, T Alpha)
    {
        // Compute the cosine of the angle between the two quaternions
        T CosAngle = A | B;

        // If the dot product is negative, negate one quaternion to take the shorter path
        TQuat<T> B2 = B;
        if (CosAngle < T(0))
        {
            B2 = -B;
            CosAngle = -CosAngle;
        }

        // If the quaternions are very close, use linear interpolation
        if (CosAngle > T(1) - MR_SMALL_NUMBER)
        {
            TQuat<T> Result = A + (B2 - A) * Alpha;
            Result.Normalize();
            return Result;
        }

        // Standard spherical interpolation
        const T Angle = std::acos(CosAngle);
        const T SinAngle = std::sin(Angle);
        const T InvSinAngle = T(1) / SinAngle;

        const T ScaleA = std::sin((T(1) - Alpha) * Angle) * InvSinAngle;
        const T ScaleB = std::sin(Alpha * Angle) * InvSinAngle;

        return A * ScaleA + B2 * ScaleB;
    }

    /**
     * Normalized linear interpolation between two quaternions
     * Faster than Slerp but less accurate for large angles
     */
    MR_NODISCARD static TQuat<T> Lerp(const TQuat<T>& A, const TQuat<T>& B, T Alpha)
    {
        // Ensure we take the shortest path
        T CosAngle = A | B;
        TQuat<T> B2 = (CosAngle >= T(0)) ? B : -B;

        TQuat<T> Result = A + (B2 - A) * Alpha;
        Result.Normalize();
        return Result;
    }

public:
    // ========================================================================
    // Member Functions
    // ========================================================================

    /** Get the squared length of the quaternion */
    MR_NODISCARD FORCEINLINE T SizeSquared() const
    {
        return X * X + Y * Y + Z * Z + W * W;
    }

    /** Get the length of the quaternion */
    MR_NODISCARD FORCEINLINE T Size() const
    {
        return std::sqrt(SizeSquared());
    }

    /** Check if the quaternion is normalized */
    MR_NODISCARD FORCEINLINE bool IsNormalized() const
    {
        return std::abs(T(1) - SizeSquared()) < MR_THRESH_QUAT_NORMALIZED;
    }

    /** Normalize this quaternion in-place */
    FORCEINLINE void Normalize(T Tolerance = MR_SMALL_NUMBER)
    {
        const T SquareSum = SizeSquared();
        if (SquareSum >= Tolerance)
        {
            const T Scale = T(1) / std::sqrt(SquareSum);
            X *= Scale;
            Y *= Scale;
            Z *= Scale;
            W *= Scale;
        }
        else
        {
            *this = Identity;
        }
    }

    /** Get a normalized copy of this quaternion */
    MR_NODISCARD FORCEINLINE TQuat<T> GetNormalized(T Tolerance = MR_SMALL_NUMBER) const
    {
        TQuat<T> Result = *this;
        Result.Normalize(Tolerance);
        return Result;
    }

    /** Check if this is the identity quaternion */
    MR_NODISCARD FORCEINLINE bool IsIdentity(T Tolerance = MR_SMALL_NUMBER) const
    {
        return Equals(Identity, Tolerance);
    }

    /** Check if two quaternions are equal within tolerance */
    MR_NODISCARD FORCEINLINE bool Equals(const TQuat<T>& Q, T Tolerance = MR_KINDA_SMALL_NUMBER) const
    {
        return (std::abs(X - Q.X) <= Tolerance && 
                std::abs(Y - Q.Y) <= Tolerance && 
                std::abs(Z - Q.Z) <= Tolerance &&
                std::abs(W - Q.W) <= Tolerance) ||
               (std::abs(X + Q.X) <= Tolerance && 
                std::abs(Y + Q.Y) <= Tolerance && 
                std::abs(Z + Q.Z) <= Tolerance &&
                std::abs(W + Q.W) <= Tolerance);
    }

    /** Get the conjugate of this quaternion */
    MR_NODISCARD FORCEINLINE TQuat<T> GetConjugate() const
    {
        return TQuat<T>(-X, -Y, -Z, W);
    }

    /** Get the inverse of this quaternion */
    MR_NODISCARD FORCEINLINE TQuat<T> Inverse() const
    {
        return GetConjugate() / SizeSquared();
    }

    /**
     * Rotate a vector by this quaternion
     * @param V The vector to rotate
     * @return The rotated vector
     */
    MR_NODISCARD FORCEINLINE TVector<T> RotateVector(const TVector<T>& V) const
    {
        // Optimized quaternion-vector rotation
        // v' = q * v * q^-1
        // Using the formula: v' = v + 2 * w * (q_xyz x v) + 2 * (q_xyz x (q_xyz x v))
        const TVector<T> Q(X, Y, Z);
        const TVector<T> T1 = TVector<T>::CrossProduct(Q, V) * T(2);
        return V + T1 * W + TVector<T>::CrossProduct(Q, T1);
    }

    /**
     * Rotate a vector by the inverse of this quaternion
     * @param V The vector to rotate
     * @return The rotated vector
     */
    MR_NODISCARD FORCEINLINE TVector<T> UnrotateVector(const TVector<T>& V) const
    {
        const TVector<T> Q(-X, -Y, -Z);
        const TVector<T> T1 = TVector<T>::CrossProduct(Q, V) * T(2);
        return V + T1 * W + TVector<T>::CrossProduct(Q, T1);
    }

    /** Get the rotation axis (normalized) */
    MR_NODISCARD FORCEINLINE TVector<T> GetRotationAxis() const
    {
        const T S = std::sqrt(std::max(T(1) - W * W, T(0)));
        if (S >= MR_SMALL_NUMBER)
        {
            return TVector<T>(X / S, Y / S, Z / S);
        }
        return TVector<T>::XAxisVector;
    }

    /** Get the rotation angle in radians */
    MR_NODISCARD FORCEINLINE T GetAngle() const
    {
        return T(2) * std::acos(std::clamp(W, T(-1), T(1)));
    }

    /** Get the axis and angle of rotation */
    FORCEINLINE void ToAxisAndAngle(TVector<T>& OutAxis, T& OutAngle) const
    {
        OutAngle = GetAngle();
        OutAxis = GetRotationAxis();
    }

    /** Get the forward direction (X axis after rotation) */
    MR_NODISCARD FORCEINLINE TVector<T> GetForwardVector() const
    {
        return RotateVector(TVector<T>::ForwardVector);
    }

    /** Get the right direction (Y axis after rotation) */
    MR_NODISCARD FORCEINLINE TVector<T> GetRightVector() const
    {
        return RotateVector(TVector<T>::RightVector);
    }

    /** Get the up direction (Z axis after rotation) */
    MR_NODISCARD FORCEINLINE TVector<T> GetUpVector() const
    {
        return RotateVector(TVector<T>::UpVector);
    }

    /** Convert to Euler angles (Rotator) */
    MR_NODISCARD TRotator<T> Rotator() const;

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

template<typename T> const TQuat<T> TQuat<T>::Identity(T(0), T(0), T(0), T(1));

// ============================================================================
// Non-member Operators
// ============================================================================

/** Scalar * Quaternion */
template<typename T>
MR_NODISCARD FORCEINLINE TQuat<T> operator*(T Scale, const TQuat<T>& Q)
{
    return Q * Scale;
}

} // namespace Math
} // namespace MonsterEngine
