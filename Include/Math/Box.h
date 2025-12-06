// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file Box.h
 * @brief Axis-Aligned Bounding Box (AABB) template class
 * 
 * This file defines the TBox<T> template class for axis-aligned bounding boxes.
 * Used for collision detection, visibility culling, and spatial queries.
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

/**
 * @brief Axis-Aligned Bounding Box template class
 * 
 * TBox represents an axis-aligned box defined by its minimum and maximum corners.
 * Used for bounding volumes, collision detection, and visibility calculations.
 * 
 * @tparam T The scalar type (float or double)
 */
template<typename T>
struct TBox
{
    static_assert(std::is_floating_point_v<T>, "TBox only supports float and double types.");

public:
    /** Type alias for the scalar type */
    using FReal = T;

    /** Minimum corner of the box */
    TVector<T> Min;

    /** Maximum corner of the box */
    TVector<T> Max;

    /** Flag indicating whether this box is valid */
    uint8_t IsValid;

public:
    // ========================================================================
    // Constructors
    // ========================================================================

    /** Default constructor (no initialization) */
    FORCEINLINE TBox() {}

    /**
     * Constructor for force initialization (creates invalid box)
     * @param E Force init enum
     */
    explicit FORCEINLINE TBox(EForceInit)
        : Min(TVector<T>::ZeroVector)
        , Max(TVector<T>::ZeroVector)
        , IsValid(0)
    {
    }

    /**
     * Constructor from min and max points
     * @param InMin Minimum corner
     * @param InMax Maximum corner
     */
    FORCEINLINE TBox(const TVector<T>& InMin, const TVector<T>& InMax)
        : Min(InMin)
        , Max(InMax)
        , IsValid(1)
    {
    }

    /**
     * Constructor from an array of points
     * @param Points Array of points
     * @param Count Number of points
     */
    TBox(const TVector<T>* Points, int32_t Count)
        : Min(TVector<T>::ZeroVector)
        , Max(TVector<T>::ZeroVector)
        , IsValid(0)
    {
        for (int32_t i = 0; i < Count; ++i)
        {
            *this += Points[i];
        }
    }

    /**
     * Copy constructor from different precision
     */
    template<typename FArg, typename = std::enable_if_t<!std::is_same_v<T, FArg>>>
    explicit FORCEINLINE TBox(const TBox<FArg>& Other)
        : Min(TVector<T>(Other.Min))
        , Max(TVector<T>(Other.Max))
        , IsValid(Other.IsValid)
    {
    }

public:
    // ========================================================================
    // Operators
    // ========================================================================

    /** Equality comparison */
    MR_NODISCARD FORCEINLINE bool operator==(const TBox<T>& Other) const
    {
        return (!IsValid && !Other.IsValid) || 
               ((IsValid && Other.IsValid) && (Min == Other.Min) && (Max == Other.Max));
    }

    /** Inequality comparison */
    MR_NODISCARD FORCEINLINE bool operator!=(const TBox<T>& Other) const
    {
        return !(*this == Other);
    }

    /** Expand box to include a point */
    FORCEINLINE TBox<T>& operator+=(const TVector<T>& Point)
    {
        if (IsValid)
        {
            Min = Min.ComponentMin(Point);
            Max = Max.ComponentMax(Point);
        }
        else
        {
            Min = Max = Point;
            IsValid = 1;
        }
        return *this;
    }

    /** Expand box to include another box */
    FORCEINLINE TBox<T>& operator+=(const TBox<T>& Other)
    {
        if (Other.IsValid)
        {
            *this += Other.Min;
            *this += Other.Max;
        }
        return *this;
    }

    /** Get a box expanded to include a point */
    MR_NODISCARD FORCEINLINE TBox<T> operator+(const TVector<T>& Point) const
    {
        TBox<T> Result = *this;
        Result += Point;
        return Result;
    }

    /** Get a box expanded to include another box */
    MR_NODISCARD FORCEINLINE TBox<T> operator+(const TBox<T>& Other) const
    {
        TBox<T> Result = *this;
        Result += Other;
        return Result;
    }

public:
    // ========================================================================
    // Member Functions
    // ========================================================================

    /** Initialize to an empty (invalid) box */
    FORCEINLINE void Init()
    {
        Min = Max = TVector<T>::ZeroVector;
        IsValid = 0;
    }

    /** Check if this box is valid */
    MR_NODISCARD FORCEINLINE bool IsValidBox() const
    {
        return IsValid != 0;
    }

    /** Check if two boxes are equal within tolerance */
    MR_NODISCARD FORCEINLINE bool Equals(const TBox<T>& Other, T Tolerance = MR_KINDA_SMALL_NUMBER) const
    {
        return (!IsValid && !Other.IsValid) || 
               ((IsValid && Other.IsValid) && Min.Equals(Other.Min, Tolerance) && Max.Equals(Other.Max, Tolerance));
    }

    /** Get the center of the box */
    MR_NODISCARD FORCEINLINE TVector<T> GetCenter() const
    {
        return (Min + Max) * T(0.5);
    }

    /** Get the extent (half-size) of the box */
    MR_NODISCARD FORCEINLINE TVector<T> GetExtent() const
    {
        return (Max - Min) * T(0.5);
    }

    /** Get the size of the box */
    MR_NODISCARD FORCEINLINE TVector<T> GetSize() const
    {
        return Max - Min;
    }

    /** Get the volume of the box */
    MR_NODISCARD FORCEINLINE T GetVolume() const
    {
        TVector<T> Size = GetSize();
        return Size.X * Size.Y * Size.Z;
    }

    /** Get the surface area of the box */
    MR_NODISCARD FORCEINLINE T GetSurfaceArea() const
    {
        TVector<T> Size = GetSize();
        return T(2) * (Size.X * Size.Y + Size.Y * Size.Z + Size.Z * Size.X);
    }

    /** Get center and extent */
    FORCEINLINE void GetCenterAndExtents(TVector<T>& OutCenter, TVector<T>& OutExtent) const
    {
        OutExtent = GetExtent();
        OutCenter = Min + OutExtent;
    }

    /** Check if a point is inside the box */
    MR_NODISCARD FORCEINLINE bool IsInside(const TVector<T>& Point) const
    {
        return IsValid &&
               Point.X >= Min.X && Point.X <= Max.X &&
               Point.Y >= Min.Y && Point.Y <= Max.Y &&
               Point.Z >= Min.Z && Point.Z <= Max.Z;
    }

    /** Check if a point is inside the box (exclusive of boundaries) */
    MR_NODISCARD FORCEINLINE bool IsInsideStrict(const TVector<T>& Point) const
    {
        return IsValid &&
               Point.X > Min.X && Point.X < Max.X &&
               Point.Y > Min.Y && Point.Y < Max.Y &&
               Point.Z > Min.Z && Point.Z < Max.Z;
    }

    /** Check if another box is completely inside this box */
    MR_NODISCARD FORCEINLINE bool IsInside(const TBox<T>& Other) const
    {
        return IsInside(Other.Min) && IsInside(Other.Max);
    }

    /** Check if this box intersects with another box */
    MR_NODISCARD FORCEINLINE bool Intersect(const TBox<T>& Other) const
    {
        if (!IsValid || !Other.IsValid)
        {
            return false;
        }

        return !(Other.Min.X > Max.X || Other.Max.X < Min.X ||
                 Other.Min.Y > Max.Y || Other.Max.Y < Min.Y ||
                 Other.Min.Z > Max.Z || Other.Max.Z < Min.Z);
    }

    /** Get the intersection of two boxes */
    MR_NODISCARD TBox<T> Overlap(const TBox<T>& Other) const
    {
        if (!Intersect(Other))
        {
            return TBox<T>(ForceInit);
        }

        return TBox<T>(
            TVector<T>(
                std::max(Min.X, Other.Min.X),
                std::max(Min.Y, Other.Min.Y),
                std::max(Min.Z, Other.Min.Z)
            ),
            TVector<T>(
                std::min(Max.X, Other.Max.X),
                std::min(Max.Y, Other.Max.Y),
                std::min(Max.Z, Other.Max.Z)
            )
        );
    }

    /** Expand the box by a scalar amount in all directions */
    MR_NODISCARD FORCEINLINE TBox<T> ExpandBy(T Amount) const
    {
        return TBox<T>(
            Min - TVector<T>(Amount),
            Max + TVector<T>(Amount)
        );
    }

    /** Expand the box by a vector amount */
    MR_NODISCARD FORCEINLINE TBox<T> ExpandBy(const TVector<T>& Amount) const
    {
        return TBox<T>(Min - Amount, Max + Amount);
    }

    /** Shift the box by a vector */
    MR_NODISCARD FORCEINLINE TBox<T> ShiftBy(const TVector<T>& Offset) const
    {
        return TBox<T>(Min + Offset, Max + Offset);
    }

    /** Move the box to a new center location */
    MR_NODISCARD FORCEINLINE TBox<T> MoveTo(const TVector<T>& NewCenter) const
    {
        TVector<T> Offset = NewCenter - GetCenter();
        return ShiftBy(Offset);
    }

    /** Get the closest point on the box to a given point */
    MR_NODISCARD FORCEINLINE TVector<T> GetClosestPointTo(const TVector<T>& Point) const
    {
        return TVector<T>(
            std::clamp(Point.X, Min.X, Max.X),
            std::clamp(Point.Y, Min.Y, Max.Y),
            std::clamp(Point.Z, Min.Z, Max.Z)
        );
    }

    /** Get the distance from a point to the box (0 if inside) */
    MR_NODISCARD FORCEINLINE T ComputeSquaredDistanceToPoint(const TVector<T>& Point) const
    {
        return (GetClosestPointTo(Point) - Point).SizeSquared();
    }

    /** Transform the box by a matrix */
    MR_NODISCARD TBox<T> TransformBy(const TMatrix<T>& M) const;

    /** Transform the box by a transform */
    MR_NODISCARD TBox<T> TransformBy(const TTransform<T>& Transform) const;

    /** Get a corner of the box (0-7) */
    MR_NODISCARD FORCEINLINE TVector<T> GetCorner(int32_t CornerIndex) const
    {
        return TVector<T>(
            (CornerIndex & 1) ? Max.X : Min.X,
            (CornerIndex & 2) ? Max.Y : Min.Y,
            (CornerIndex & 4) ? Max.Z : Min.Z
        );
    }

    /** Convert to a string representation */
    MR_NODISCARD inline std::string ToString() const
    {
        char Buffer[256];
        snprintf(Buffer, sizeof(Buffer), "IsValid=%d, Min=(%s), Max=(%s)",
                 IsValid, Min.ToString().c_str(), Max.ToString().c_str());
        return std::string(Buffer);
    }

public:
    // ========================================================================
    // Static Functions
    // ========================================================================

    /** Build a box from center and extent */
    MR_NODISCARD static FORCEINLINE TBox<T> BuildAABB(const TVector<T>& Center, const TVector<T>& Extent)
    {
        return TBox<T>(Center - Extent, Center + Extent);
    }
};

} // namespace Math
} // namespace MonsterEngine
