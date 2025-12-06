// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file Rotator.h
 * @brief Euler angles rotation template class
 * 
 * This file defines the TRotator<T> template class for Euler angle-based rotations.
 * Rotators use Pitch, Yaw, and Roll angles in degrees.
 * Supports both float and double precision following UE5's LWC pattern.
 */

#include "MathFwd.h"
#include "MathUtility.h"
#include "Vector.h"
#include "Quat.h"
#include <cmath>
#include <string>
#include <type_traits>

namespace MonsterEngine
{
namespace Math
{

/**
 * @brief Euler angles rotation template class
 * 
 * TRotator represents a rotation using Pitch, Yaw, and Roll angles in degrees.
 * 
 * The angles are interpreted as intrinsic rotations applied in the order:
 * Yaw (Z) -> Pitch (Y) -> Roll (X)
 * 
 * - Pitch: Rotation around the right axis (Y), looking up/down
 * - Yaw: Rotation around the up axis (Z), turning left/right
 * - Roll: Rotation around the forward axis (X), tilting
 * 
 * Note: Rotators can suffer from gimbal lock. For complex rotations,
 * consider using TQuat instead.
 * 
 * @tparam T The scalar type (float or double)
 */
template<typename T>
struct TRotator
{
    static_assert(std::is_floating_point_v<T>, "TRotator only supports float and double types.");

public:
    /** Type alias for the scalar type */
    using FReal = T;

    /** Rotation around the right axis (Y), looking up/down. Positive = up */
    T Pitch;

    /** Rotation around the up axis (Z), turning left/right. Positive = right */
    T Yaw;

    /** Rotation around the forward axis (X), tilting. Positive = clockwise */
    T Roll;

public:
    // ========================================================================
    // Static Constants
    // ========================================================================

    /** A zero rotator (no rotation) */
    static const TRotator<T> ZeroRotator;

public:
    // ========================================================================
    // Constructors
    // ========================================================================

    /** Default constructor (no initialization for performance) */
    FORCEINLINE TRotator() {}

    /**
     * Constructor initializing all angles to a single value
     * @param InF Value to set all angles to (in degrees)
     */
    explicit FORCEINLINE TRotator(T InF)
        : Pitch(InF), Yaw(InF), Roll(InF)
    {
        DiagnosticCheckNaN();
    }

    /**
     * Constructor using initial values for each angle
     * @param InPitch Pitch angle in degrees
     * @param InYaw Yaw angle in degrees
     * @param InRoll Roll angle in degrees
     */
    FORCEINLINE TRotator(T InPitch, T InYaw, T InRoll)
        : Pitch(InPitch), Yaw(InYaw), Roll(InRoll)
    {
        DiagnosticCheckNaN();
    }

    /**
     * Constructor for force initialization
     * @param E Force init enum
     */
    explicit FORCEINLINE TRotator(EForceInit)
        : Pitch(T(0)), Yaw(T(0)), Roll(T(0))
    {
    }

    /**
     * Constructor from quaternion
     * @param Q The quaternion to convert from
     */
    explicit TRotator(const TQuat<T>& Q);

    /**
     * Copy constructor from different precision
     * @param R Rotator to copy from
     */
    template<typename FArg, typename = std::enable_if_t<!std::is_same_v<T, FArg>>>
    explicit FORCEINLINE TRotator(const TRotator<FArg>& R)
        : Pitch(static_cast<T>(R.Pitch))
        , Yaw(static_cast<T>(R.Yaw))
        , Roll(static_cast<T>(R.Roll))
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
            *const_cast<TRotator<T>*>(this) = ZeroRotator;
        }
    }

    FORCEINLINE void DiagnosticCheckNaN(const char* Message) const
    {
        if (ContainsNaN())
        {
            *const_cast<TRotator<T>*>(this) = ZeroRotator;
        }
    }
#else
    FORCEINLINE void DiagnosticCheckNaN() const {}
    FORCEINLINE void DiagnosticCheckNaN(const char* Message) const {}
#endif

    /** Check if any component is NaN */
    MR_NODISCARD FORCEINLINE bool ContainsNaN() const
    {
        return !std::isfinite(Pitch) || !std::isfinite(Yaw) || !std::isfinite(Roll);
    }

public:
    // ========================================================================
    // Operators
    // ========================================================================

    /** Addition of two rotators */
    MR_NODISCARD FORCEINLINE TRotator<T> operator+(const TRotator<T>& R) const
    {
        return TRotator<T>(Pitch + R.Pitch, Yaw + R.Yaw, Roll + R.Roll);
    }

    /** Subtraction of two rotators */
    MR_NODISCARD FORCEINLINE TRotator<T> operator-(const TRotator<T>& R) const
    {
        return TRotator<T>(Pitch - R.Pitch, Yaw - R.Yaw, Roll - R.Roll);
    }

    /** Scalar multiplication */
    MR_NODISCARD FORCEINLINE TRotator<T> operator*(T Scale) const
    {
        return TRotator<T>(Pitch * Scale, Yaw * Scale, Roll * Scale);
    }

    /** Scalar division */
    MR_NODISCARD FORCEINLINE TRotator<T> operator/(T Scale) const
    {
        const T RScale = T(1) / Scale;
        return TRotator<T>(Pitch * RScale, Yaw * RScale, Roll * RScale);
    }

    /** Negation */
    MR_NODISCARD FORCEINLINE TRotator<T> operator-() const
    {
        return TRotator<T>(-Pitch, -Yaw, -Roll);
    }

    /** Addition assignment */
    FORCEINLINE TRotator<T>& operator+=(const TRotator<T>& R)
    {
        Pitch += R.Pitch; Yaw += R.Yaw; Roll += R.Roll;
        DiagnosticCheckNaN();
        return *this;
    }

    /** Subtraction assignment */
    FORCEINLINE TRotator<T>& operator-=(const TRotator<T>& R)
    {
        Pitch -= R.Pitch; Yaw -= R.Yaw; Roll -= R.Roll;
        DiagnosticCheckNaN();
        return *this;
    }

    /** Scalar multiplication assignment */
    FORCEINLINE TRotator<T>& operator*=(T Scale)
    {
        Pitch *= Scale; Yaw *= Scale; Roll *= Scale;
        DiagnosticCheckNaN();
        return *this;
    }

    /** Scalar division assignment */
    FORCEINLINE TRotator<T>& operator/=(T Scale)
    {
        const T RScale = T(1) / Scale;
        Pitch *= RScale; Yaw *= RScale; Roll *= RScale;
        DiagnosticCheckNaN();
        return *this;
    }

    /** Equality comparison */
    MR_NODISCARD FORCEINLINE bool operator==(const TRotator<T>& R) const
    {
        return Pitch == R.Pitch && Yaw == R.Yaw && Roll == R.Roll;
    }

    /** Inequality comparison */
    MR_NODISCARD FORCEINLINE bool operator!=(const TRotator<T>& R) const
    {
        return Pitch != R.Pitch || Yaw != R.Yaw || Roll != R.Roll;
    }

public:
    // ========================================================================
    // Static Functions
    // ========================================================================

    /**
     * Clamp an angle to the range [0, 360)
     */
    MR_NODISCARD static FORCEINLINE T ClampAxis(T Angle)
    {
        Angle = std::fmod(Angle, T(360));
        if (Angle < T(0))
        {
            Angle += T(360);
        }
        return Angle;
    }

    /**
     * Clamp an angle to the range [-180, 180)
     */
    MR_NODISCARD static FORCEINLINE T NormalizeAxis(T Angle)
    {
        Angle = ClampAxis(Angle);
        if (Angle > T(180))
        {
            Angle -= T(360);
        }
        return Angle;
    }

    /**
     * Find the shortest rotation between two angles
     */
    MR_NODISCARD static FORCEINLINE T FindDeltaAngleDegrees(T A1, T A2)
    {
        T Delta = A2 - A1;
        if (Delta > T(180))
        {
            Delta -= T(360);
        }
        else if (Delta < T(-180))
        {
            Delta += T(360);
        }
        return Delta;
    }

public:
    // ========================================================================
    // Member Functions
    // ========================================================================

    /** Set all angles */
    FORCEINLINE void Set(T InPitch, T InYaw, T InRoll)
    {
        Pitch = InPitch;
        Yaw = InYaw;
        Roll = InRoll;
        DiagnosticCheckNaN();
    }

    /** Check if the rotator is nearly zero */
    MR_NODISCARD FORCEINLINE bool IsNearlyZero(T Tolerance = MR_KINDA_SMALL_NUMBER) const
    {
        return std::abs(NormalizeAxis(Pitch)) <= Tolerance &&
               std::abs(NormalizeAxis(Yaw)) <= Tolerance &&
               std::abs(NormalizeAxis(Roll)) <= Tolerance;
    }

    /** Check if the rotator is exactly zero */
    MR_NODISCARD FORCEINLINE bool IsZero() const
    {
        return ClampAxis(Pitch) == T(0) && ClampAxis(Yaw) == T(0) && ClampAxis(Roll) == T(0);
    }

    /** Check if two rotators are equal within tolerance */
    MR_NODISCARD FORCEINLINE bool Equals(const TRotator<T>& R, T Tolerance = MR_KINDA_SMALL_NUMBER) const
    {
        return std::abs(NormalizeAxis(Pitch - R.Pitch)) <= Tolerance &&
               std::abs(NormalizeAxis(Yaw - R.Yaw)) <= Tolerance &&
               std::abs(NormalizeAxis(Roll - R.Roll)) <= Tolerance;
    }

    /**
     * Clamp all angles to [0, 360)
     */
    MR_NODISCARD FORCEINLINE TRotator<T> GetClamped() const
    {
        return TRotator<T>(ClampAxis(Pitch), ClampAxis(Yaw), ClampAxis(Roll));
    }

    /**
     * Normalize all angles to [-180, 180)
     */
    MR_NODISCARD FORCEINLINE TRotator<T> GetNormalized() const
    {
        return TRotator<T>(NormalizeAxis(Pitch), NormalizeAxis(Yaw), NormalizeAxis(Roll));
    }

    /**
     * Normalize this rotator in-place
     */
    FORCEINLINE void Normalize()
    {
        Pitch = NormalizeAxis(Pitch);
        Yaw = NormalizeAxis(Yaw);
        Roll = NormalizeAxis(Roll);
    }

    /**
     * Get the inverse of this rotator
     */
    MR_NODISCARD FORCEINLINE TRotator<T> GetInverse() const
    {
        return Quaternion().Inverse().Rotator();
    }

    /**
     * Convert to quaternion
     */
    MR_NODISCARD TQuat<T> Quaternion() const
    {
        // Convert degrees to radians and compute half angles
        const T PitchRad = Pitch * (MR_PI / T(180));
        const T YawRad = Yaw * (MR_PI / T(180));
        const T RollRad = Roll * (MR_PI / T(180));

        const T SP = std::sin(PitchRad * T(0.5));
        const T CP = std::cos(PitchRad * T(0.5));
        const T SY = std::sin(YawRad * T(0.5));
        const T CY = std::cos(YawRad * T(0.5));
        const T SR = std::sin(RollRad * T(0.5));
        const T CR = std::cos(RollRad * T(0.5));

        // Rotation order: Yaw -> Pitch -> Roll
        return TQuat<T>(
            CR * SP * SY - SR * CP * CY,
            -CR * SP * CY - SR * CP * SY,
            CR * CP * SY - SR * SP * CY,
            CR * CP * CY + SR * SP * SY
        );
    }

    /**
     * Rotate a vector by this rotator
     */
    MR_NODISCARD FORCEINLINE TVector<T> RotateVector(const TVector<T>& V) const
    {
        return Quaternion().RotateVector(V);
    }

    /**
     * Rotate a vector by the inverse of this rotator
     */
    MR_NODISCARD FORCEINLINE TVector<T> UnrotateVector(const TVector<T>& V) const
    {
        return Quaternion().UnrotateVector(V);
    }

    /**
     * Get the forward direction (X axis after rotation)
     */
    MR_NODISCARD FORCEINLINE TVector<T> GetForwardVector() const
    {
        return RotateVector(TVector<T>::ForwardVector);
    }

    /**
     * Get the right direction (Y axis after rotation)
     */
    MR_NODISCARD FORCEINLINE TVector<T> GetRightVector() const
    {
        return RotateVector(TVector<T>::RightVector);
    }

    /**
     * Get the up direction (Z axis after rotation)
     */
    MR_NODISCARD FORCEINLINE TVector<T> GetUpVector() const
    {
        return RotateVector(TVector<T>::UpVector);
    }

    /**
     * Combine this rotator with another (not commutative!)
     */
    MR_NODISCARD FORCEINLINE TRotator<T> Add(const TRotator<T>& R) const
    {
        return (Quaternion() * R.Quaternion()).Rotator();
    }

    /**
     * Linear interpolation between two rotators
     */
    MR_NODISCARD static FORCEINLINE TRotator<T> Lerp(const TRotator<T>& A, const TRotator<T>& B, T Alpha)
    {
        return TRotator<T>(
            A.Pitch + FindDeltaAngleDegrees(A.Pitch, B.Pitch) * Alpha,
            A.Yaw + FindDeltaAngleDegrees(A.Yaw, B.Yaw) * Alpha,
            A.Roll + FindDeltaAngleDegrees(A.Roll, B.Roll) * Alpha
        );
    }

    /** Convert to a string representation */
    MR_NODISCARD inline std::string ToString() const
    {
        char Buffer[128];
        snprintf(Buffer, sizeof(Buffer), "P=%.6f Y=%.6f R=%.6f", 
                 static_cast<double>(Pitch), static_cast<double>(Yaw), static_cast<double>(Roll));
        return std::string(Buffer);
    }
};

// ============================================================================
// Static Constant Definitions
// ============================================================================

template<typename T> const TRotator<T> TRotator<T>::ZeroRotator(T(0), T(0), T(0));

// ============================================================================
// TQuat Constructor from TRotator
// ============================================================================

template<typename T>
TQuat<T>::TQuat(const TRotator<T>& R)
{
    *this = R.Quaternion();
}

// ============================================================================
// TQuat::Rotator() Implementation
// ============================================================================

template<typename T>
TRotator<T> TQuat<T>::Rotator() const
{
    T SingularityTest = Z * X - W * Y;
    T YawY = T(2) * (W * Z + X * Y);
    T YawX = T(1) - T(2) * (Y * Y + Z * Z);

    // Reference: https://www.euclideanspace.com/maths/geometry/rotations/conversions/quaternionToEuler/
    const T SingularityThreshold = T(0.4999995);

    T Pitch, Yaw, Roll;

    if (SingularityTest < -SingularityThreshold)
    {
        // Gimbal lock at south pole
        Pitch = T(-90);
        Yaw = std::atan2(YawY, YawX) * (T(180) / MR_PI);
        Roll = TRotator<T>::NormalizeAxis(-Yaw - T(2) * std::atan2(X, W) * (T(180) / MR_PI));
    }
    else if (SingularityTest > SingularityThreshold)
    {
        // Gimbal lock at north pole
        Pitch = T(90);
        Yaw = std::atan2(YawY, YawX) * (T(180) / MR_PI);
        Roll = TRotator<T>::NormalizeAxis(Yaw - T(2) * std::atan2(X, W) * (T(180) / MR_PI));
    }
    else
    {
        // Normal case
        Pitch = std::asin(T(2) * SingularityTest) * (T(180) / MR_PI);
        Yaw = std::atan2(YawY, YawX) * (T(180) / MR_PI);
        Roll = std::atan2(T(-2) * (W * X + Y * Z), T(1) - T(2) * (X * X + Y * Y)) * (T(180) / MR_PI);
    }

    return TRotator<T>(Pitch, Yaw, Roll);
}

// ============================================================================
// TRotator Constructor from TQuat
// ============================================================================

template<typename T>
TRotator<T>::TRotator(const TQuat<T>& Q)
{
    *this = Q.Rotator();
}

// ============================================================================
// Non-member Operators
// ============================================================================

/** Scalar * Rotator */
template<typename T>
MR_NODISCARD FORCEINLINE TRotator<T> operator*(T Scale, const TRotator<T>& R)
{
    return R * Scale;
}

} // namespace Math
} // namespace MonsterEngine
