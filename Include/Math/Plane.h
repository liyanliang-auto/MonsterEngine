// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file Plane.h
 * @brief Plane template class
 * 
 * This file defines the TPlane<T> template class for 3D planes.
 * Used for collision detection, clipping, and spatial queries.
 * Supports both float and double precision following UE5's LWC pattern.
 */

#include "MathFwd.h"
#include "MathUtility.h"
#include "Vector.h"
#include "Vector4.h"
#include <cmath>
#include <type_traits>

namespace MonsterEngine
{
namespace Math
{

/**
 * @brief Plane template class
 * 
 * TPlane represents a plane in 3D space using the equation: Ax + By + Cz = D
 * where (A, B, C) is the plane normal and D is the distance from origin.
 * 
 * The plane is stored as (X, Y, Z, W) where:
 * - (X, Y, Z) is the plane normal
 * - W is the distance from origin along the normal
 * 
 * @tparam T The scalar type (float or double)
 */
template<typename T>
struct TPlane : public TVector<T>
{
    static_assert(std::is_floating_point_v<T>, "TPlane only supports float and double types.");

public:
    /** Type alias for the scalar type */
    using FReal = T;

    /** Distance from origin along the normal */
    T W;

public:
    // ========================================================================
    // Constructors
    // ========================================================================

    /** Default constructor (no initialization) */
    FORCEINLINE TPlane() {}

    /**
     * Constructor for force initialization
     * @param E Force init enum
     */
    explicit FORCEINLINE TPlane(EForceInit)
        : TVector<T>(ForceInit)
        , W(T(0))
    {
    }

    /**
     * Constructor from normal and distance
     * @param InNormal Plane normal (should be normalized)
     * @param InW Distance from origin
     */
    FORCEINLINE TPlane(const TVector<T>& InNormal, T InW)
        : TVector<T>(InNormal)
        , W(InW)
    {
    }

    /**
     * Constructor from 4 values
     * @param InX X component of normal
     * @param InY Y component of normal
     * @param InZ Z component of normal
     * @param InW Distance from origin
     */
    FORCEINLINE TPlane(T InX, T InY, T InZ, T InW)
        : TVector<T>(InX, InY, InZ)
        , W(InW)
    {
    }

    /**
     * Constructor from TVector4
     * @param V 4D vector (XYZ = normal, W = distance)
     */
    FORCEINLINE TPlane(const TVector4<T>& V)
        : TVector<T>(V.X, V.Y, V.Z)
        , W(V.W)
    {
    }

    /**
     * Constructor from a point on the plane and a normal
     * @param Point A point on the plane
     * @param Normal The plane normal (should be normalized)
     */
    FORCEINLINE TPlane(const TVector<T>& Point, const TVector<T>& Normal)
        : TVector<T>(Normal)
        , W(TVector<T>::DotProduct(Point, Normal))
    {
    }

    /**
     * Constructor from three points on the plane
     * @param A First point
     * @param B Second point
     * @param C Third point
     */
    TPlane(const TVector<T>& A, const TVector<T>& B, const TVector<T>& C)
    {
        TVector<T> Normal = TVector<T>::CrossProduct(B - A, C - A).GetSafeNormal();
        this->X = Normal.X;
        this->Y = Normal.Y;
        this->Z = Normal.Z;
        W = TVector<T>::DotProduct(A, Normal);
    }

    /**
     * Copy constructor from different precision
     */
    template<typename FArg, typename = std::enable_if_t<!std::is_same_v<T, FArg>>>
    explicit FORCEINLINE TPlane(const TPlane<FArg>& Other)
        : TVector<T>(static_cast<T>(Other.X), static_cast<T>(Other.Y), static_cast<T>(Other.Z))
        , W(static_cast<T>(Other.W))
    {
    }

public:
    // ========================================================================
    // Operators
    // ========================================================================

    /** Equality comparison */
    MR_NODISCARD FORCEINLINE bool operator==(const TPlane<T>& Other) const
    {
        return this->X == Other.X && this->Y == Other.Y && this->Z == Other.Z && W == Other.W;
    }

    /** Inequality comparison */
    MR_NODISCARD FORCEINLINE bool operator!=(const TPlane<T>& Other) const
    {
        return !(*this == Other);
    }

    /** Scalar multiplication */
    MR_NODISCARD FORCEINLINE TPlane<T> operator*(T Scale) const
    {
        return TPlane<T>(this->X * Scale, this->Y * Scale, this->Z * Scale, W * Scale);
    }

    /** Scalar division */
    MR_NODISCARD FORCEINLINE TPlane<T> operator/(T Scale) const
    {
        T RScale = T(1) / Scale;
        return TPlane<T>(this->X * RScale, this->Y * RScale, this->Z * RScale, W * RScale);
    }

    /** Negation (flip the plane) */
    MR_NODISCARD FORCEINLINE TPlane<T> operator-() const
    {
        return TPlane<T>(-this->X, -this->Y, -this->Z, -W);
    }

    /** Dot product with a point (signed distance) */
    MR_NODISCARD FORCEINLINE T operator|(const TVector<T>& V) const
    {
        return this->X * V.X + this->Y * V.Y + this->Z * V.Z - W;
    }

public:
    // ========================================================================
    // Member Functions
    // ========================================================================

    /** Get the plane normal */
    MR_NODISCARD FORCEINLINE TVector<T> GetNormal() const
    {
        return TVector<T>(this->X, this->Y, this->Z);
    }

    /** Get the origin point (closest point to world origin) */
    MR_NODISCARD FORCEINLINE TVector<T> GetOrigin() const
    {
        return GetNormal() * W;
    }

    /** Check if two planes are equal within tolerance */
    MR_NODISCARD FORCEINLINE bool Equals(const TPlane<T>& Other, T Tolerance = MR_KINDA_SMALL_NUMBER) const
    {
        return std::abs(this->X - Other.X) <= Tolerance &&
               std::abs(this->Y - Other.Y) <= Tolerance &&
               std::abs(this->Z - Other.Z) <= Tolerance &&
               std::abs(W - Other.W) <= Tolerance;
    }

    /**
     * Calculate the signed distance from a point to the plane
     * Positive = in front of plane (same side as normal)
     * Negative = behind plane
     * Zero = on plane
     */
    MR_NODISCARD FORCEINLINE T PlaneDot(const TVector<T>& Point) const
    {
        return this->X * Point.X + this->Y * Point.Y + this->Z * Point.Z - W;
    }

    /** Check if a point is in front of the plane */
    MR_NODISCARD FORCEINLINE bool IsInFront(const TVector<T>& Point) const
    {
        return PlaneDot(Point) > T(0);
    }

    /** Check if a point is behind the plane */
    MR_NODISCARD FORCEINLINE bool IsBehind(const TVector<T>& Point) const
    {
        return PlaneDot(Point) < T(0);
    }

    /** Check if a point is on the plane (within tolerance) */
    MR_NODISCARD FORCEINLINE bool IsOnPlane(const TVector<T>& Point, T Tolerance = MR_KINDA_SMALL_NUMBER) const
    {
        return std::abs(PlaneDot(Point)) <= Tolerance;
    }

    /** Get the closest point on the plane to a given point */
    MR_NODISCARD FORCEINLINE TVector<T> GetClosestPointTo(const TVector<T>& Point) const
    {
        return Point - GetNormal() * PlaneDot(Point);
    }

    /** Project a point onto the plane */
    MR_NODISCARD FORCEINLINE TVector<T> ProjectPoint(const TVector<T>& Point) const
    {
        return GetClosestPointTo(Point);
    }

    /** Mirror a point across the plane */
    MR_NODISCARD FORCEINLINE TVector<T> MirrorPoint(const TVector<T>& Point) const
    {
        return Point - GetNormal() * (T(2) * PlaneDot(Point));
    }

    /** Mirror a vector across the plane (direction only) */
    MR_NODISCARD FORCEINLINE TVector<T> MirrorVector(const TVector<T>& V) const
    {
        return V - GetNormal() * (T(2) * TVector<T>::DotProduct(V, GetNormal()));
    }

    /** Normalize the plane (make normal unit length) */
    MR_NODISCARD TPlane<T> GetNormalized() const
    {
        T Size = GetNormal().Size();
        if (Size > MR_SMALL_NUMBER)
        {
            T InvSize = T(1) / Size;
            return TPlane<T>(this->X * InvSize, this->Y * InvSize, this->Z * InvSize, W * InvSize);
        }
        return *this;
    }

    /** Flip the plane (reverse normal direction) */
    MR_NODISCARD FORCEINLINE TPlane<T> Flip() const
    {
        return TPlane<T>(-this->X, -this->Y, -this->Z, -W);
    }

    /** Transform the plane by a matrix */
    MR_NODISCARD TPlane<T> TransformBy(const TMatrix<T>& M) const;

    /** Convert to TVector4 */
    MR_NODISCARD FORCEINLINE TVector4<T> ToVector4() const
    {
        return TVector4<T>(this->X, this->Y, this->Z, W);
    }

    /** Convert to a string representation */
    MR_NODISCARD inline std::string ToString() const
    {
        char Buffer[128];
        snprintf(Buffer, sizeof(Buffer), "Normal=(%s), W=%.6f",
                 GetNormal().ToString().c_str(), static_cast<double>(W));
        return std::string(Buffer);
    }

public:
    // ========================================================================
    // Static Functions
    // ========================================================================

    /**
     * Calculate the intersection point of a line and a plane
     * @param RayOrigin Origin of the ray
     * @param RayDirection Direction of the ray (should be normalized)
     * @param Plane The plane to intersect with
     * @param OutT Output: distance along ray to intersection
     * @return True if intersection exists
     */
    static bool RayPlaneIntersection(const TVector<T>& RayOrigin, const TVector<T>& RayDirection,
                                     const TPlane<T>& Plane, T& OutT)
    {
        T Denom = TVector<T>::DotProduct(RayDirection, Plane.GetNormal());
        if (std::abs(Denom) > MR_SMALL_NUMBER)
        {
            OutT = (Plane.W - TVector<T>::DotProduct(RayOrigin, Plane.GetNormal())) / Denom;
            return true;
        }
        return false;
    }

    /**
     * Calculate the intersection point of a line segment and a plane
     * @param Start Start of the line segment
     * @param End End of the line segment
     * @param Plane The plane to intersect with
     * @param OutIntersection Output: intersection point
     * @return True if intersection exists within the segment
     */
    static bool LinePlaneIntersection(const TVector<T>& Start, const TVector<T>& End,
                                      const TPlane<T>& Plane, TVector<T>& OutIntersection)
    {
        TVector<T> Direction = End - Start;
        T T_Val;
        if (RayPlaneIntersection(Start, Direction.GetSafeNormal(), Plane, T_Val))
        {
            T Length = Direction.Size();
            if (T_Val >= T(0) && T_Val <= Length)
            {
                OutIntersection = Start + Direction.GetSafeNormal() * T_Val;
                return true;
            }
        }
        return false;
    }

    /**
     * Calculate the intersection line of two planes
     * @param Plane1 First plane
     * @param Plane2 Second plane
     * @param OutLineDirection Output: direction of the intersection line
     * @param OutLinePoint Output: a point on the intersection line
     * @return True if planes intersect (not parallel)
     */
    static bool PlanePlaneIntersection(const TPlane<T>& Plane1, const TPlane<T>& Plane2,
                                       TVector<T>& OutLineDirection, TVector<T>& OutLinePoint)
    {
        OutLineDirection = TVector<T>::CrossProduct(Plane1.GetNormal(), Plane2.GetNormal());
        T Denom = OutLineDirection.SizeSquared();

        if (Denom < MR_SMALL_NUMBER)
        {
            return false; // Planes are parallel
        }

        // Find a point on the intersection line
        OutLinePoint = (TVector<T>::CrossProduct(OutLineDirection, Plane2.GetNormal()) * Plane1.W +
                        TVector<T>::CrossProduct(Plane1.GetNormal(), OutLineDirection) * Plane2.W) / Denom;

        OutLineDirection = OutLineDirection.GetSafeNormal();
        return true;
    }
};

} // namespace Math
} // namespace MonsterEngine
