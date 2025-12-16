// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file Matrix.h
 * @brief 4x4 Matrix template class
 * 
 * This file defines the TMatrix<T> template class for 4x4 matrix operations.
 * Used for transformations, projections, and coordinate space conversions.
 * Supports both float and double precision following UE5's LWC pattern.
 */

#include "MathFwd.h"
#include "MathUtility.h"
#include "Vector.h"
#include "Vector4.h"
#include "Rotator.h"
#include "Quat.h"
#include <cmath>
#include <cstring>
#include <string>
#include <type_traits>

namespace MonsterEngine
{
namespace Math
{

/**
 * @brief A 4x4 matrix template class
 * 
 * TMatrix represents a 4x4 transformation matrix.
 * Matrix elements are accessed with M[RowIndex][ColumnIndex].
 * 
 * Matrix multiplication is row-major: Result = A * B means
 * transform by B first, then by A.
 * 
 * @tparam T The scalar type (float or double)
 */
template<typename T>
struct alignas(16) TMatrix
{
    static_assert(std::is_floating_point_v<T>, "TMatrix only supports float and double types.");

public:
    /** Type alias for the scalar type */
    using FReal = T;

    /** The matrix elements [Row][Column] */
    alignas(16) T M[4][4];

public:
    // ========================================================================
    // Static Constants
    // ========================================================================

    /** Identity matrix */
    static const TMatrix<T> Identity;

public:
    // ========================================================================
    // Constructors
    // ========================================================================

    /** Default constructor (no initialization for performance) */
    FORCEINLINE TMatrix() {}

    /**
     * Constructor for force initialization (zero matrix)
     * @param E Force init enum
     */
    explicit FORCEINLINE TMatrix(EForceInit)
    {
        std::memset(M, 0, sizeof(M));
    }

    /**
     * Constructor from 4 row vectors
     * @param InX First row
     * @param InY Second row
     * @param InZ Third row
     * @param InW Fourth row
     */
    FORCEINLINE TMatrix(const TVector<T>& InX, const TVector<T>& InY, 
                        const TVector<T>& InZ, const TVector<T>& InW)
    {
        M[0][0] = InX.X; M[0][1] = InX.Y; M[0][2] = InX.Z; M[0][3] = T(0);
        M[1][0] = InY.X; M[1][1] = InY.Y; M[1][2] = InY.Z; M[1][3] = T(0);
        M[2][0] = InZ.X; M[2][1] = InZ.Y; M[2][2] = InZ.Z; M[2][3] = T(0);
        M[3][0] = InW.X; M[3][1] = InW.Y; M[3][2] = InW.Z; M[3][3] = T(1);
    }

    /**
     * Constructor from 4 TVector4 row vectors
     */
    FORCEINLINE TMatrix(const TVector4<T>& InX, const TVector4<T>& InY,
                        const TVector4<T>& InZ, const TVector4<T>& InW)
    {
        M[0][0] = InX.X; M[0][1] = InX.Y; M[0][2] = InX.Z; M[0][3] = InX.W;
        M[1][0] = InY.X; M[1][1] = InY.Y; M[1][2] = InY.Z; M[1][3] = InY.W;
        M[2][0] = InZ.X; M[2][1] = InZ.Y; M[2][2] = InZ.Z; M[2][3] = InZ.W;
        M[3][0] = InW.X; M[3][1] = InW.Y; M[3][2] = InW.Z; M[3][3] = InW.W;
    }

    /**
     * Copy constructor from different precision
     */
    template<typename FArg, typename = std::enable_if_t<!std::is_same_v<T, FArg>>>
    explicit FORCEINLINE TMatrix(const TMatrix<FArg>& Other)
    {
        for (int32_t i = 0; i < 4; ++i)
        {
            for (int32_t j = 0; j < 4; ++j)
            {
                M[i][j] = static_cast<T>(Other.M[i][j]);
            }
        }
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
            *const_cast<TMatrix<T>*>(this) = Identity;
        }
    }
#else
    FORCEINLINE void DiagnosticCheckNaN() const {}
#endif

    /** Check if any element is NaN */
    MR_NODISCARD FORCEINLINE bool ContainsNaN() const
    {
        for (int32_t i = 0; i < 4; ++i)
        {
            for (int32_t j = 0; j < 4; ++j)
            {
                if (!std::isfinite(M[i][j]))
                {
                    return true;
                }
            }
        }
        return false;
    }

public:
    // ========================================================================
    // Operators
    // ========================================================================

    /** Set this matrix to identity */
    FORCEINLINE void SetIdentity()
    {
        M[0][0] = T(1); M[0][1] = T(0); M[0][2] = T(0); M[0][3] = T(0);
        M[1][0] = T(0); M[1][1] = T(1); M[1][2] = T(0); M[1][3] = T(0);
        M[2][0] = T(0); M[2][1] = T(0); M[2][2] = T(1); M[2][3] = T(0);
        M[3][0] = T(0); M[3][1] = T(0); M[3][2] = T(0); M[3][3] = T(1);
    }

    /** Matrix multiplication */
    MR_NODISCARD TMatrix<T> operator*(const TMatrix<T>& Other) const
    {
        TMatrix<T> Result;
        for (int32_t i = 0; i < 4; ++i)
        {
            for (int32_t j = 0; j < 4; ++j)
            {
                Result.M[i][j] = M[i][0] * Other.M[0][j] +
                                 M[i][1] * Other.M[1][j] +
                                 M[i][2] * Other.M[2][j] +
                                 M[i][3] * Other.M[3][j];
            }
        }
        return Result;
    }

    /** Matrix multiplication assignment */
    FORCEINLINE void operator*=(const TMatrix<T>& Other)
    {
        *this = *this * Other;
    }

    /** Matrix addition */
    MR_NODISCARD FORCEINLINE TMatrix<T> operator+(const TMatrix<T>& Other) const
    {
        TMatrix<T> Result;
        for (int32_t i = 0; i < 4; ++i)
        {
            for (int32_t j = 0; j < 4; ++j)
            {
                Result.M[i][j] = M[i][j] + Other.M[i][j];
            }
        }
        return Result;
    }

    /** Matrix addition assignment */
    FORCEINLINE void operator+=(const TMatrix<T>& Other)
    {
        for (int32_t i = 0; i < 4; ++i)
        {
            for (int32_t j = 0; j < 4; ++j)
            {
                M[i][j] += Other.M[i][j];
            }
        }
    }

    /** Scalar multiplication */
    MR_NODISCARD FORCEINLINE TMatrix<T> operator*(T Scale) const
    {
        TMatrix<T> Result;
        for (int32_t i = 0; i < 4; ++i)
        {
            for (int32_t j = 0; j < 4; ++j)
            {
                Result.M[i][j] = M[i][j] * Scale;
            }
        }
        return Result;
    }

    /** Scalar multiplication assignment */
    FORCEINLINE void operator*=(T Scale)
    {
        for (int32_t i = 0; i < 4; ++i)
        {
            for (int32_t j = 0; j < 4; ++j)
            {
                M[i][j] *= Scale;
            }
        }
    }

    /** Equality comparison */
    MR_NODISCARD bool operator==(const TMatrix<T>& Other) const
    {
        for (int32_t i = 0; i < 4; ++i)
        {
            for (int32_t j = 0; j < 4; ++j)
            {
                if (M[i][j] != Other.M[i][j])
                {
                    return false;
                }
            }
        }
        return true;
    }

    /** Inequality comparison */
    MR_NODISCARD FORCEINLINE bool operator!=(const TMatrix<T>& Other) const
    {
        return !(*this == Other);
    }

public:
    // ========================================================================
    // Transform Operations
    // ========================================================================

    /** Transform a 4D vector */
    MR_NODISCARD FORCEINLINE TVector4<T> TransformFVector4(const TVector4<T>& V) const
    {
        return TVector4<T>(
            M[0][0] * V.X + M[1][0] * V.Y + M[2][0] * V.Z + M[3][0] * V.W,
            M[0][1] * V.X + M[1][1] * V.Y + M[2][1] * V.Z + M[3][1] * V.W,
            M[0][2] * V.X + M[1][2] * V.Y + M[2][2] * V.Z + M[3][2] * V.W,
            M[0][3] * V.X + M[1][3] * V.Y + M[2][3] * V.Z + M[3][3] * V.W
        );
    }

    /** Transform a position (applies translation) */
    MR_NODISCARD FORCEINLINE TVector4<T> TransformPosition(const TVector<T>& V) const
    {
        return TransformFVector4(TVector4<T>(V.X, V.Y, V.Z, T(1)));
    }

    /** Transform a direction (ignores translation) */
    MR_NODISCARD FORCEINLINE TVector4<T> TransformVector(const TVector<T>& V) const
    {
        return TransformFVector4(TVector4<T>(V.X, V.Y, V.Z, T(0)));
    }

    /** Inverse transform a position */
    MR_NODISCARD FORCEINLINE TVector<T> InverseTransformPosition(const TVector<T>& V) const
    {
        TMatrix<T> InvSelf = Inverse();
        return InvSelf.TransformPosition(V).GetXYZ();
    }

    /** Inverse transform a direction */
    MR_NODISCARD FORCEINLINE TVector<T> InverseTransformVector(const TVector<T>& V) const
    {
        TMatrix<T> InvSelf = Inverse();
        return InvSelf.TransformVector(V).GetXYZ();
    }

public:
    // ========================================================================
    // Matrix Operations
    // ========================================================================

    /** Get the transpose of this matrix */
    MR_NODISCARD FORCEINLINE TMatrix<T> GetTransposed() const
    {
        TMatrix<T> Result;
        for (int32_t i = 0; i < 4; ++i)
        {
            for (int32_t j = 0; j < 4; ++j)
            {
                Result.M[i][j] = M[j][i];
            }
        }
        return Result;
    }

    /** Calculate the determinant */
    MR_NODISCARD T Determinant() const
    {
        return
            M[0][0] * (
                M[1][1] * (M[2][2] * M[3][3] - M[2][3] * M[3][2]) -
                M[2][1] * (M[1][2] * M[3][3] - M[1][3] * M[3][2]) +
                M[3][1] * (M[1][2] * M[2][3] - M[1][3] * M[2][2])
            ) -
            M[1][0] * (
                M[0][1] * (M[2][2] * M[3][3] - M[2][3] * M[3][2]) -
                M[2][1] * (M[0][2] * M[3][3] - M[0][3] * M[3][2]) +
                M[3][1] * (M[0][2] * M[2][3] - M[0][3] * M[2][2])
            ) +
            M[2][0] * (
                M[0][1] * (M[1][2] * M[3][3] - M[1][3] * M[3][2]) -
                M[1][1] * (M[0][2] * M[3][3] - M[0][3] * M[3][2]) +
                M[3][1] * (M[0][2] * M[1][3] - M[0][3] * M[1][2])
            ) -
            M[3][0] * (
                M[0][1] * (M[1][2] * M[2][3] - M[1][3] * M[2][2]) -
                M[1][1] * (M[0][2] * M[2][3] - M[0][3] * M[2][2]) +
                M[2][1] * (M[0][2] * M[1][3] - M[0][3] * M[1][2])
            );
    }

    /** Calculate the inverse of this matrix */
    MR_NODISCARD TMatrix<T> Inverse() const
    {
        TMatrix<T> Result;

        T Det = Determinant();
        if (std::abs(Det) < MR_SMALL_NUMBER)
        {
            return Identity;
        }

        T InvDet = T(1) / Det;

        // Calculate cofactor matrix and transpose
        Result.M[0][0] = InvDet * (M[1][1] * (M[2][2] * M[3][3] - M[2][3] * M[3][2]) - M[2][1] * (M[1][2] * M[3][3] - M[1][3] * M[3][2]) + M[3][1] * (M[1][2] * M[2][3] - M[1][3] * M[2][2]));
        Result.M[0][1] = InvDet * -(M[0][1] * (M[2][2] * M[3][3] - M[2][3] * M[3][2]) - M[2][1] * (M[0][2] * M[3][3] - M[0][3] * M[3][2]) + M[3][1] * (M[0][2] * M[2][3] - M[0][3] * M[2][2]));
        Result.M[0][2] = InvDet * (M[0][1] * (M[1][2] * M[3][3] - M[1][3] * M[3][2]) - M[1][1] * (M[0][2] * M[3][3] - M[0][3] * M[3][2]) + M[3][1] * (M[0][2] * M[1][3] - M[0][3] * M[1][2]));
        Result.M[0][3] = InvDet * -(M[0][1] * (M[1][2] * M[2][3] - M[1][3] * M[2][2]) - M[1][1] * (M[0][2] * M[2][3] - M[0][3] * M[2][2]) + M[2][1] * (M[0][2] * M[1][3] - M[0][3] * M[1][2]));

        Result.M[1][0] = InvDet * -(M[1][0] * (M[2][2] * M[3][3] - M[2][3] * M[3][2]) - M[2][0] * (M[1][2] * M[3][3] - M[1][3] * M[3][2]) + M[3][0] * (M[1][2] * M[2][3] - M[1][3] * M[2][2]));
        Result.M[1][1] = InvDet * (M[0][0] * (M[2][2] * M[3][3] - M[2][3] * M[3][2]) - M[2][0] * (M[0][2] * M[3][3] - M[0][3] * M[3][2]) + M[3][0] * (M[0][2] * M[2][3] - M[0][3] * M[2][2]));
        Result.M[1][2] = InvDet * -(M[0][0] * (M[1][2] * M[3][3] - M[1][3] * M[3][2]) - M[1][0] * (M[0][2] * M[3][3] - M[0][3] * M[3][2]) + M[3][0] * (M[0][2] * M[1][3] - M[0][3] * M[1][2]));
        Result.M[1][3] = InvDet * (M[0][0] * (M[1][2] * M[2][3] - M[1][3] * M[2][2]) - M[1][0] * (M[0][2] * M[2][3] - M[0][3] * M[2][2]) + M[2][0] * (M[0][2] * M[1][3] - M[0][3] * M[1][2]));

        Result.M[2][0] = InvDet * (M[1][0] * (M[2][1] * M[3][3] - M[2][3] * M[3][1]) - M[2][0] * (M[1][1] * M[3][3] - M[1][3] * M[3][1]) + M[3][0] * (M[1][1] * M[2][3] - M[1][3] * M[2][1]));
        Result.M[2][1] = InvDet * -(M[0][0] * (M[2][1] * M[3][3] - M[2][3] * M[3][1]) - M[2][0] * (M[0][1] * M[3][3] - M[0][3] * M[3][1]) + M[3][0] * (M[0][1] * M[2][3] - M[0][3] * M[2][1]));
        Result.M[2][2] = InvDet * (M[0][0] * (M[1][1] * M[3][3] - M[1][3] * M[3][1]) - M[1][0] * (M[0][1] * M[3][3] - M[0][3] * M[3][1]) + M[3][0] * (M[0][1] * M[1][3] - M[0][3] * M[1][1]));
        Result.M[2][3] = InvDet * -(M[0][0] * (M[1][1] * M[2][3] - M[1][3] * M[2][1]) - M[1][0] * (M[0][1] * M[2][3] - M[0][3] * M[2][1]) + M[2][0] * (M[0][1] * M[1][3] - M[0][3] * M[1][1]));

        Result.M[3][0] = InvDet * -(M[1][0] * (M[2][1] * M[3][2] - M[2][2] * M[3][1]) - M[2][0] * (M[1][1] * M[3][2] - M[1][2] * M[3][1]) + M[3][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]));
        Result.M[3][1] = InvDet * (M[0][0] * (M[2][1] * M[3][2] - M[2][2] * M[3][1]) - M[2][0] * (M[0][1] * M[3][2] - M[0][2] * M[3][1]) + M[3][0] * (M[0][1] * M[2][2] - M[0][2] * M[2][1]));
        Result.M[3][2] = InvDet * -(M[0][0] * (M[1][1] * M[3][2] - M[1][2] * M[3][1]) - M[1][0] * (M[0][1] * M[3][2] - M[0][2] * M[3][1]) + M[3][0] * (M[0][1] * M[1][2] - M[0][2] * M[1][1]));
        Result.M[3][3] = InvDet * (M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) - M[1][0] * (M[0][1] * M[2][2] - M[0][2] * M[2][1]) + M[2][0] * (M[0][1] * M[1][2] - M[0][2] * M[1][1]));

        return Result;
    }

    /** Get the scale from this matrix (assumes no shear) */
    MR_NODISCARD FORCEINLINE TVector<T> GetScaleVector() const
    {
        return TVector<T>(
            TVector<T>(M[0][0], M[0][1], M[0][2]).Size(),
            TVector<T>(M[1][0], M[1][1], M[1][2]).Size(),
            TVector<T>(M[2][0], M[2][1], M[2][2]).Size()
        );
    }

    /** Get the origin (translation) from this matrix */
    MR_NODISCARD FORCEINLINE TVector<T> GetOrigin() const
    {
        return TVector<T>(M[3][0], M[3][1], M[3][2]);
    }

    /** Set the origin (translation) of this matrix */
    FORCEINLINE void SetOrigin(const TVector<T>& NewOrigin)
    {
        M[3][0] = NewOrigin.X;
        M[3][1] = NewOrigin.Y;
        M[3][2] = NewOrigin.Z;
    }

    /** Get the X axis of this matrix */
    MR_NODISCARD FORCEINLINE TVector<T> GetAxisX() const
    {
        return TVector<T>(M[0][0], M[0][1], M[0][2]);
    }

    /** Get the Y axis of this matrix */
    MR_NODISCARD FORCEINLINE TVector<T> GetAxisY() const
    {
        return TVector<T>(M[1][0], M[1][1], M[1][2]);
    }

    /** Get the Z axis of this matrix */
    MR_NODISCARD FORCEINLINE TVector<T> GetAxisZ() const
    {
        return TVector<T>(M[2][0], M[2][1], M[2][2]);
    }

    /** Check if two matrices are equal within tolerance */
    MR_NODISCARD bool Equals(const TMatrix<T>& Other, T Tolerance = MR_KINDA_SMALL_NUMBER) const
    {
        for (int32_t i = 0; i < 4; ++i)
        {
            for (int32_t j = 0; j < 4; ++j)
            {
                if (std::abs(M[i][j] - Other.M[i][j]) > Tolerance)
                {
                    return false;
                }
            }
        }
        return true;
    }

    /** Convert to a string representation */
    MR_NODISCARD inline std::string ToString() const
    {
        char Buffer[512];
        snprintf(Buffer, sizeof(Buffer),
            "[%.3f %.3f %.3f %.3f]\n[%.3f %.3f %.3f %.3f]\n[%.3f %.3f %.3f %.3f]\n[%.3f %.3f %.3f %.3f]",
            static_cast<double>(M[0][0]), static_cast<double>(M[0][1]), static_cast<double>(M[0][2]), static_cast<double>(M[0][3]),
            static_cast<double>(M[1][0]), static_cast<double>(M[1][1]), static_cast<double>(M[1][2]), static_cast<double>(M[1][3]),
            static_cast<double>(M[2][0]), static_cast<double>(M[2][1]), static_cast<double>(M[2][2]), static_cast<double>(M[2][3]),
            static_cast<double>(M[3][0]), static_cast<double>(M[3][1]), static_cast<double>(M[3][2]), static_cast<double>(M[3][3])
        );
        return std::string(Buffer);
    }

public:
    // ========================================================================
    // Static Factory Functions
    // ========================================================================

    /** Create a translation matrix */
    MR_NODISCARD static TMatrix<T> MakeTranslation(const TVector<T>& Translation)
    {
        TMatrix<T> Result;
        Result.SetIdentity();
        Result.M[3][0] = Translation.X;
        Result.M[3][1] = Translation.Y;
        Result.M[3][2] = Translation.Z;
        return Result;
    }

    /** Create a scale matrix */
    MR_NODISCARD static TMatrix<T> MakeScale(const TVector<T>& Scale)
    {
        TMatrix<T> Result(ForceInit);
        Result.M[0][0] = Scale.X;
        Result.M[1][1] = Scale.Y;
        Result.M[2][2] = Scale.Z;
        Result.M[3][3] = T(1);
        return Result;
    }

    /** Create a uniform scale matrix */
    MR_NODISCARD static TMatrix<T> MakeScale(T Scale)
    {
        return MakeScale(TVector<T>(Scale, Scale, Scale));
    }

    /** Create a rotation matrix from a quaternion */
    MR_NODISCARD static TMatrix<T> MakeFromQuat(const TQuat<T>& Q)
    {
        TMatrix<T> Result;

        const T X2 = Q.X + Q.X;
        const T Y2 = Q.Y + Q.Y;
        const T Z2 = Q.Z + Q.Z;

        const T XX = Q.X * X2;
        const T XY = Q.X * Y2;
        const T XZ = Q.X * Z2;

        const T YY = Q.Y * Y2;
        const T YZ = Q.Y * Z2;
        const T ZZ = Q.Z * Z2;

        const T WX = Q.W * X2;
        const T WY = Q.W * Y2;
        const T WZ = Q.W * Z2;

        Result.M[0][0] = T(1) - (YY + ZZ);
        Result.M[0][1] = XY + WZ;
        Result.M[0][2] = XZ - WY;
        Result.M[0][3] = T(0);

        Result.M[1][0] = XY - WZ;
        Result.M[1][1] = T(1) - (XX + ZZ);
        Result.M[1][2] = YZ + WX;
        Result.M[1][3] = T(0);

        Result.M[2][0] = XZ + WY;
        Result.M[2][1] = YZ - WX;
        Result.M[2][2] = T(1) - (XX + YY);
        Result.M[2][3] = T(0);

        Result.M[3][0] = T(0);
        Result.M[3][1] = T(0);
        Result.M[3][2] = T(0);
        Result.M[3][3] = T(1);

        return Result;
    }

    /** Create a rotation matrix from a rotator */
    MR_NODISCARD static TMatrix<T> MakeFromRotator(const TRotator<T>& R)
    {
        return MakeFromQuat(R.Quaternion());
    }

    /** Create a look-at matrix (row-major layout: M[Row][Col]) */
    MR_NODISCARD static TMatrix<T> MakeLookAt(const TVector<T>& Eye, const TVector<T>& Target, const TVector<T>& Up)
    {
        // Forward direction (from eye to target, then negated for right-handed coordinate system)
        TVector<T> Forward = (Eye - Target).GetSafeNormal();  // Points away from target
        TVector<T> Right = TVector<T>::CrossProduct(Up, Forward).GetSafeNormal();
        TVector<T> UpVec = TVector<T>::CrossProduct(Forward, Right);

        // Row-major layout for view matrix:
        // Row 0: Right vector components
        // Row 1: Up vector components
        // Row 2: Forward vector components
        // Row 3: Translation (eye position transformed to view space)
        //
        // Standard row-major view matrix:
        // | Rx  Ry  Rz  0 |
        // | Ux  Uy  Uz  0 |
        // | Fx  Fy  Fz  0 |
        // | Tx  Ty  Tz  1 |
        // Where T = -dot(axis, Eye)
        TMatrix<T> Result;
        Result.M[0][0] = Right.X;   Result.M[0][1] = Right.Y;   Result.M[0][2] = Right.Z;   Result.M[0][3] = T(0);
        Result.M[1][0] = UpVec.X;   Result.M[1][1] = UpVec.Y;   Result.M[1][2] = UpVec.Z;   Result.M[1][3] = T(0);
        Result.M[2][0] = Forward.X; Result.M[2][1] = Forward.Y; Result.M[2][2] = Forward.Z; Result.M[2][3] = T(0);
        Result.M[3][0] = -TVector<T>::DotProduct(Right, Eye);
        Result.M[3][1] = -TVector<T>::DotProduct(UpVec, Eye);
        Result.M[3][2] = -TVector<T>::DotProduct(Forward, Eye);
        Result.M[3][3] = T(1);

        return Result;
    }

    /** 
     * Create a perspective projection matrix for Vulkan (Y-down, Z [0,1])
     * Assumes right-handed view space where camera looks toward -Z
     * (objects in front of camera have negative Z in view space)
     * Row-major layout: M[Row][Col]
     */
    MR_NODISCARD static TMatrix<T> MakePerspective(T FovY, T AspectRatio, T NearZ, T FarZ)
    {
        const T TanHalfFov = std::tan(FovY * T(0.5));

        TMatrix<T> Result(ForceInit);
        
        // Row-major perspective matrix for right-handed coordinate system
        // Row 0: [sx,  0,  0,  0]
        // Row 1: [ 0, sy,  0,  0]
        // Row 2: [ 0,  0, sz, tz]
        // Row 3: [ 0,  0, -1,  0]
        Result.M[0][0] = T(1) / (AspectRatio * TanHalfFov);
        Result.M[1][1] = T(1) / TanHalfFov;  // Will be flipped by Vulkan Y-flip if needed
        Result.M[2][2] = FarZ / (NearZ - FarZ);  // Note: NearZ - FarZ for -Z direction
        Result.M[2][3] = (FarZ * NearZ) / (NearZ - FarZ);
        Result.M[3][2] = T(-1);  // -1 for right-handed (camera looks toward -Z)

        return Result;
    }
    
    /** 
     * Create a perspective projection matrix for OpenGL (Z [-1,1])
     * Standard OpenGL right-handed coordinate system
     * Row-major layout: M[Row][Col]
     */
    MR_NODISCARD static TMatrix<T> MakePerspectiveGL(T FovY, T AspectRatio, T NearZ, T FarZ)
    {
        const T TanHalfFov = std::tan(FovY * T(0.5));

        TMatrix<T> Result(ForceInit);
        
        // Row-major OpenGL perspective matrix with depth range [-1, 1]
        // Row 0: [sx,  0,  0,  0]
        // Row 1: [ 0, sy,  0,  0]
        // Row 2: [ 0,  0, sz, tz]
        // Row 3: [ 0,  0, -1,  0]
        Result.M[0][0] = T(1) / (AspectRatio * TanHalfFov);
        Result.M[1][1] = T(1) / TanHalfFov;
        Result.M[2][2] = -(FarZ + NearZ) / (FarZ - NearZ);
        Result.M[2][3] = -(T(2) * FarZ * NearZ) / (FarZ - NearZ);
        Result.M[3][2] = T(-1);

        return Result;
    }

    /** Create an orthographic projection matrix */
    MR_NODISCARD static TMatrix<T> MakeOrtho(T Width, T Height, T NearZ, T FarZ)
    {
        TMatrix<T> Result(ForceInit);
        Result.M[0][0] = T(2) / Width;
        Result.M[1][1] = T(2) / Height;
        Result.M[2][2] = T(1) / (FarZ - NearZ);
        Result.M[3][2] = -NearZ / (FarZ - NearZ);
        Result.M[3][3] = T(1);

        return Result;
    }
};

// ============================================================================
// Static Constant Definitions
// ============================================================================

template<typename T>
const TMatrix<T> TMatrix<T>::Identity = []()
{
    TMatrix<T> M;
    M.SetIdentity();
    return M;
}();

// ============================================================================
// TQuat Constructor from TMatrix
// ============================================================================

template<typename T>
TQuat<T>::TQuat(const TMatrix<T>& M)
{
    // Algorithm from: http://www.euclideanspace.com/maths/geometry/rotations/conversions/matrixToQuaternion/
    T Trace = M.M[0][0] + M.M[1][1] + M.M[2][2];

    if (Trace > T(0))
    {
        T S = std::sqrt(Trace + T(1)) * T(2);
        W = T(0.25) * S;
        X = (M.M[1][2] - M.M[2][1]) / S;
        Y = (M.M[2][0] - M.M[0][2]) / S;
        Z = (M.M[0][1] - M.M[1][0]) / S;
    }
    else if (M.M[0][0] > M.M[1][1] && M.M[0][0] > M.M[2][2])
    {
        T S = std::sqrt(T(1) + M.M[0][0] - M.M[1][1] - M.M[2][2]) * T(2);
        W = (M.M[1][2] - M.M[2][1]) / S;
        X = T(0.25) * S;
        Y = (M.M[1][0] + M.M[0][1]) / S;
        Z = (M.M[2][0] + M.M[0][2]) / S;
    }
    else if (M.M[1][1] > M.M[2][2])
    {
        T S = std::sqrt(T(1) + M.M[1][1] - M.M[0][0] - M.M[2][2]) * T(2);
        W = (M.M[2][0] - M.M[0][2]) / S;
        X = (M.M[1][0] + M.M[0][1]) / S;
        Y = T(0.25) * S;
        Z = (M.M[2][1] + M.M[1][2]) / S;
    }
    else
    {
        T S = std::sqrt(T(1) + M.M[2][2] - M.M[0][0] - M.M[1][1]) * T(2);
        W = (M.M[0][1] - M.M[1][0]) / S;
        X = (M.M[2][0] + M.M[0][2]) / S;
        Y = (M.M[2][1] + M.M[1][2]) / S;
        Z = T(0.25) * S;
    }

    Normalize();
}

} // namespace Math
} // namespace MonsterEngine
