// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file Sphere.h
 * @brief Bounding Sphere template class
 * 
 * This file defines the TSphere<T> template class for bounding spheres.
 * Used for collision detection, visibility culling, and spatial queries.
 * Supports both float and double precision following UE5's LWC pattern.
 */

#include "MathFwd.h"
#include "MathUtility.h"
#include "Vector.h"
#include "Box.h"
#include <cmath>
#include <type_traits>

namespace MonsterEngine
{
namespace Math
{

/**
 * @brief Bounding Sphere template class
 * 
 * TSphere represents a sphere defined by its center and radius.
 * Used for bounding volumes, collision detection, and visibility calculations.
 * 
 * @tparam T The scalar type (float or double)
 */
template<typename T>
struct TSphere
{
    static_assert(std::is_floating_point_v<T>, "TSphere only supports float and double types.");

public:
    /** Type alias for the scalar type */
    using FReal = T;

    /** Center of the sphere */
    TVector<T> Center;

    /** Radius of the sphere (stored as W for compatibility) */
    T W;

public:
    // ========================================================================
    // Constructors
    // ========================================================================

    /** Default constructor (no initialization) */
    FORCEINLINE TSphere() {}

    /**
     * Constructor for force initialization
     * @param E Force init enum
     */
    explicit FORCEINLINE TSphere(EForceInit)
        : Center(TVector<T>::ZeroVector)
        , W(T(0))
    {
    }

    /**
     * Constructor from center and radius
     * @param InCenter Center of the sphere
     * @param InRadius Radius of the sphere
     */
    FORCEINLINE TSphere(const TVector<T>& InCenter, T InRadius)
        : Center(InCenter)
        , W(InRadius)
    {
    }

    /**
     * Constructor from 4 values (X, Y, Z, W)
     * @param InX X component of center
     * @param InY Y component of center
     * @param InZ Z component of center
     * @param InW Radius
     */
    FORCEINLINE TSphere(T InX, T InY, T InZ, T InW)
        : Center(InX, InY, InZ)
        , W(InW)
    {
    }

    /**
     * Constructor from an array of points (computes bounding sphere)
     * @param Points Array of points
     * @param Count Number of points
     */
    TSphere(const TVector<T>* Points, int32_t Count)
        : Center(TVector<T>::ZeroVector)
        , W(T(0))
    {
        if (Count > 0)
        {
            // Compute bounding box first
            TBox<T> Box(ForceInit);
            for (int32_t i = 0; i < Count; ++i)
            {
                Box += Points[i];
            }

            // Use box center as sphere center
            Center = Box.GetCenter();

            // Find maximum distance from center
            T MaxDistSq = T(0);
            for (int32_t i = 0; i < Count; ++i)
            {
                T DistSq = (Points[i] - Center).SizeSquared();
                if (DistSq > MaxDistSq)
                {
                    MaxDistSq = DistSq;
                }
            }
            W = std::sqrt(MaxDistSq);
        }
    }

    /**
     * Copy constructor from different precision
     */
    template<typename FArg, typename = std::enable_if_t<!std::is_same_v<T, FArg>>>
    explicit FORCEINLINE TSphere(const TSphere<FArg>& Other)
        : Center(TVector<T>(Other.Center))
        , W(static_cast<T>(Other.W))
    {
    }

public:
    // ========================================================================
    // Operators
    // ========================================================================

    /** Equality comparison */
    MR_NODISCARD FORCEINLINE bool operator==(const TSphere<T>& Other) const
    {
        return Center == Other.Center && W == Other.W;
    }

    /** Inequality comparison */
    MR_NODISCARD FORCEINLINE bool operator!=(const TSphere<T>& Other) const
    {
        return !(*this == Other);
    }

public:
    // ========================================================================
    // Member Functions
    // ========================================================================

    /** Get the radius of the sphere */
    MR_NODISCARD FORCEINLINE T GetRadius() const
    {
        return W;
    }

    /** Set the radius of the sphere */
    FORCEINLINE void SetRadius(T InRadius)
    {
        W = InRadius;
    }

    /** Check if two spheres are equal within tolerance */
    MR_NODISCARD FORCEINLINE bool Equals(const TSphere<T>& Other, T Tolerance = MR_KINDA_SMALL_NUMBER) const
    {
        return Center.Equals(Other.Center, Tolerance) && std::abs(W - Other.W) <= Tolerance;
    }

    /** Check if a point is inside the sphere */
    MR_NODISCARD FORCEINLINE bool IsInside(const TVector<T>& Point, T Tolerance = MR_KINDA_SMALL_NUMBER) const
    {
        return (Point - Center).SizeSquared() <= (W + Tolerance) * (W + Tolerance);
    }

    /** Check if a point is strictly inside the sphere */
    MR_NODISCARD FORCEINLINE bool IsInsideStrict(const TVector<T>& Point) const
    {
        return (Point - Center).SizeSquared() < W * W;
    }

    /** Check if another sphere is completely inside this sphere */
    MR_NODISCARD FORCEINLINE bool IsInside(const TSphere<T>& Other, T Tolerance = MR_KINDA_SMALL_NUMBER) const
    {
        if (W < Other.W - Tolerance)
        {
            return false;
        }
        return (Other.Center - Center).Size() + Other.W <= W + Tolerance;
    }

    /** Check if this sphere intersects with another sphere */
    MR_NODISCARD FORCEINLINE bool Intersects(const TSphere<T>& Other, T Tolerance = MR_KINDA_SMALL_NUMBER) const
    {
        T RadiusSum = W + Other.W + Tolerance;
        return (Center - Other.Center).SizeSquared() <= RadiusSum * RadiusSum;
    }

    /** Check if this sphere intersects with a box */
    MR_NODISCARD FORCEINLINE bool Intersects(const TBox<T>& Box, T Tolerance = MR_KINDA_SMALL_NUMBER) const
    {
        T DistSq = Box.ComputeSquaredDistanceToPoint(Center);
        T RadiusPlusTol = W + Tolerance;
        return DistSq <= RadiusPlusTol * RadiusPlusTol;
    }

    /** Get the distance from a point to the sphere surface (negative if inside) */
    MR_NODISCARD FORCEINLINE T GetDistanceToPoint(const TVector<T>& Point) const
    {
        return (Point - Center).Size() - W;
    }

    /** Get the closest point on the sphere surface to a given point */
    MR_NODISCARD FORCEINLINE TVector<T> GetClosestPointTo(const TVector<T>& Point) const
    {
        TVector<T> Dir = (Point - Center).GetSafeNormal();
        return Center + Dir * W;
    }

    /** Get the volume of the sphere */
    MR_NODISCARD FORCEINLINE T GetVolume() const
    {
        return (T(4) / T(3)) * T(MR_PI) * W * W * W;
    }

    /** Get the surface area of the sphere */
    MR_NODISCARD FORCEINLINE T GetSurfaceArea() const
    {
        return T(4) * T(MR_PI) * W * W;
    }

    /** Expand the sphere to include a point */
    FORCEINLINE TSphere<T>& operator+=(const TVector<T>& Point)
    {
        if (W < T(0))
        {
            Center = Point;
            W = T(0);
        }
        else
        {
            T Dist = (Point - Center).Size();
            if (Dist > W)
            {
                T NewRadius = (W + Dist) * T(0.5);
                Center = Center + (Point - Center).GetSafeNormal() * (NewRadius - W);
                W = NewRadius;
            }
        }
        return *this;
    }

    /** Expand the sphere to include another sphere */
    FORCEINLINE TSphere<T>& operator+=(const TSphere<T>& Other)
    {
        if (W < T(0))
        {
            *this = Other;
        }
        else if (Other.W >= T(0))
        {
            TVector<T> Dir = Other.Center - Center;
            T Dist = Dir.Size();

            if (Dist + Other.W > W)
            {
                if (Dist + W <= Other.W)
                {
                    // Other sphere contains this one
                    *this = Other;
                }
                else
                {
                    // Compute new bounding sphere
                    T NewRadius = (Dist + W + Other.W) * T(0.5);
                    Center = Center + Dir.GetSafeNormal() * (NewRadius - W);
                    W = NewRadius;
                }
            }
        }
        return *this;
    }

    /** Get a sphere expanded to include a point */
    MR_NODISCARD FORCEINLINE TSphere<T> operator+(const TVector<T>& Point) const
    {
        TSphere<T> Result = *this;
        Result += Point;
        return Result;
    }

    /** Get a sphere expanded to include another sphere */
    MR_NODISCARD FORCEINLINE TSphere<T> operator+(const TSphere<T>& Other) const
    {
        TSphere<T> Result = *this;
        Result += Other;
        return Result;
    }

    /** Transform the sphere by a matrix */
    MR_NODISCARD TSphere<T> TransformBy(const TMatrix<T>& M) const;

    /** Transform the sphere by a transform */
    MR_NODISCARD TSphere<T> TransformBy(const TTransform<T>& Transform) const;

    /** Convert to a string representation */
    MR_NODISCARD inline std::string ToString() const
    {
        char Buffer[128];
        snprintf(Buffer, sizeof(Buffer), "Center=(%s), Radius=%.6f",
                 Center.ToString().c_str(), static_cast<double>(W));
        return std::string(Buffer);
    }

public:
    // ========================================================================
    // Static Functions
    // ========================================================================

    /** Compute a bounding sphere that contains two spheres */
    MR_NODISCARD static TSphere<T> ComputeBoundingSphere(const TSphere<T>& A, const TSphere<T>& B)
    {
        TSphere<T> Result = A;
        Result += B;
        return Result;
    }
};

} // namespace Math
} // namespace MonsterEngine
