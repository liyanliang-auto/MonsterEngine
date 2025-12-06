// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file Transform.h
 * @brief Combined transformation template class (Translation + Rotation + Scale)
 * 
 * This file defines the TTransform<T> template class that combines translation,
 * rotation (quaternion), and scale into a single transformation representation.
 * This is the most commonly used transform type in game engines.
 * Supports both float and double precision following UE5's LWC pattern.
 */

#include "MathFwd.h"
#include "MathUtility.h"
#include "Vector.h"
#include "Quat.h"
#include "Rotator.h"
#include "Matrix.h"
#include <cmath>
#include <string>
#include <type_traits>

namespace MonsterEngine
{
namespace Math
{

/**
 * @brief Combined transformation template class
 * 
 * TTransform represents a complete 3D transformation consisting of:
 * - Translation (TVector<T>)
 * - Rotation (TQuat<T>)
 * - Scale (TVector<T>)
 * 
 * Transform order: Scale -> Rotate -> Translate
 * When composing transforms: Child * Parent
 * 
 * @tparam T The scalar type (float or double)
 */
template<typename T>
struct alignas(16) TTransform
{
    static_assert(std::is_floating_point_v<T>, "TTransform only supports float and double types.");

public:
    /** Type alias for the scalar type */
    using FReal = T;

protected:
    /** Rotation quaternion */
    TQuat<T> Rotation;

    /** Translation vector */
    TVector<T> Translation;

    /** 3D scale vector */
    TVector<T> Scale3D;

public:
    // ========================================================================
    // Static Constants
    // ========================================================================

    /** Identity transform (no translation, no rotation, unit scale) */
    static const TTransform<T> Identity;

public:
    // ========================================================================
    // Constructors
    // ========================================================================

    /** Default constructor - initializes to identity */
    FORCEINLINE TTransform()
        : Rotation(TQuat<T>::Identity)
        , Translation(TVector<T>::ZeroVector)
        , Scale3D(TVector<T>::OneVector)
    {
    }

    /**
     * Constructor for force initialization
     * @param E Force init enum
     */
    explicit FORCEINLINE TTransform(EForceInit)
        : Rotation(ForceInit)
        , Translation(ForceInit)
        , Scale3D(TVector<T>::OneVector)
    {
    }

    /**
     * Constructor with translation only
     * @param InTranslation The translation
     */
    explicit FORCEINLINE TTransform(const TVector<T>& InTranslation)
        : Rotation(TQuat<T>::Identity)
        , Translation(InTranslation)
        , Scale3D(TVector<T>::OneVector)
    {
    }

    /**
     * Constructor with rotation only
     * @param InRotation The rotation quaternion
     */
    explicit FORCEINLINE TTransform(const TQuat<T>& InRotation)
        : Rotation(InRotation)
        , Translation(TVector<T>::ZeroVector)
        , Scale3D(TVector<T>::OneVector)
    {
    }

    /**
     * Constructor with rotator only
     * @param InRotation The rotation as Euler angles
     */
    explicit FORCEINLINE TTransform(const TRotator<T>& InRotation)
        : Rotation(InRotation.Quaternion())
        , Translation(TVector<T>::ZeroVector)
        , Scale3D(TVector<T>::OneVector)
    {
    }

    /**
     * Constructor with rotation and translation
     * @param InRotation The rotation quaternion
     * @param InTranslation The translation
     */
    FORCEINLINE TTransform(const TQuat<T>& InRotation, const TVector<T>& InTranslation)
        : Rotation(InRotation)
        , Translation(InTranslation)
        , Scale3D(TVector<T>::OneVector)
    {
    }

    /**
     * Constructor with rotation, translation, and scale
     * @param InRotation The rotation quaternion
     * @param InTranslation The translation
     * @param InScale3D The 3D scale
     */
    FORCEINLINE TTransform(const TQuat<T>& InRotation, const TVector<T>& InTranslation, const TVector<T>& InScale3D)
        : Rotation(InRotation)
        , Translation(InTranslation)
        , Scale3D(InScale3D)
    {
    }

    /**
     * Constructor with rotator, translation, and scale
     * @param InRotation The rotation as Euler angles
     * @param InTranslation The translation
     * @param InScale3D The 3D scale
     */
    FORCEINLINE TTransform(const TRotator<T>& InRotation, const TVector<T>& InTranslation, const TVector<T>& InScale3D)
        : Rotation(InRotation.Quaternion())
        , Translation(InTranslation)
        , Scale3D(InScale3D)
    {
    }

    /**
     * Constructor from matrix
     * @param InMatrix The transformation matrix
     */
    explicit TTransform(const TMatrix<T>& InMatrix)
    {
        SetFromMatrix(InMatrix);
    }

    /**
     * Copy constructor from different precision
     */
    template<typename FArg, typename = std::enable_if_t<!std::is_same_v<T, FArg>>>
    explicit FORCEINLINE TTransform(const TTransform<FArg>& Other)
        : Rotation(TQuat<T>(Other.GetRotation()))
        , Translation(TVector<T>(Other.GetTranslation()))
        , Scale3D(TVector<T>(Other.GetScale3D()))
    {
    }

public:
    // ========================================================================
    // NaN Diagnostics
    // ========================================================================

#if ENABLE_NAN_DIAGNOSTIC
    FORCEINLINE void DiagnosticCheckNaN_All() const
    {
        if (ContainsNaN())
        {
            *const_cast<TTransform<T>*>(this) = Identity;
        }
    }

    FORCEINLINE void DiagnosticCheckNaN_Translate() const
    {
        Translation.DiagnosticCheckNaN();
    }

    FORCEINLINE void DiagnosticCheckNaN_Rotate() const
    {
        Rotation.DiagnosticCheckNaN();
    }

    FORCEINLINE void DiagnosticCheckNaN_Scale3D() const
    {
        Scale3D.DiagnosticCheckNaN();
    }
#else
    FORCEINLINE void DiagnosticCheckNaN_All() const {}
    FORCEINLINE void DiagnosticCheckNaN_Translate() const {}
    FORCEINLINE void DiagnosticCheckNaN_Rotate() const {}
    FORCEINLINE void DiagnosticCheckNaN_Scale3D() const {}
#endif

    /** Check if any component contains NaN */
    MR_NODISCARD FORCEINLINE bool ContainsNaN() const
    {
        return Translation.ContainsNaN() || Rotation.ContainsNaN() || Scale3D.ContainsNaN();
    }

public:
    // ========================================================================
    // Accessors
    // ========================================================================

    /** Get the rotation quaternion */
    MR_NODISCARD FORCEINLINE TQuat<T> GetRotation() const
    {
        return Rotation;
    }

    /** Get the rotation as Euler angles */
    MR_NODISCARD FORCEINLINE TRotator<T> Rotator() const
    {
        return Rotation.Rotator();
    }

    /** Get the translation vector */
    MR_NODISCARD FORCEINLINE TVector<T> GetTranslation() const
    {
        return Translation;
    }

    /** Get the location (alias for GetTranslation) */
    MR_NODISCARD FORCEINLINE TVector<T> GetLocation() const
    {
        return Translation;
    }

    /** Get the 3D scale vector */
    MR_NODISCARD FORCEINLINE TVector<T> GetScale3D() const
    {
        return Scale3D;
    }

    /** Set the rotation quaternion */
    FORCEINLINE void SetRotation(const TQuat<T>& InRotation)
    {
        Rotation = InRotation;
        DiagnosticCheckNaN_Rotate();
    }

    /** Set the translation vector */
    FORCEINLINE void SetTranslation(const TVector<T>& InTranslation)
    {
        Translation = InTranslation;
        DiagnosticCheckNaN_Translate();
    }

    /** Set the location (alias for SetTranslation) */
    FORCEINLINE void SetLocation(const TVector<T>& InLocation)
    {
        SetTranslation(InLocation);
    }

    /** Set the 3D scale vector */
    FORCEINLINE void SetScale3D(const TVector<T>& InScale3D)
    {
        Scale3D = InScale3D;
        DiagnosticCheckNaN_Scale3D();
    }

    /** Set all components */
    FORCEINLINE void SetComponents(const TQuat<T>& InRotation, const TVector<T>& InTranslation, const TVector<T>& InScale3D)
    {
        Rotation = InRotation;
        Translation = InTranslation;
        Scale3D = InScale3D;
        DiagnosticCheckNaN_All();
    }

    /** Set to identity transform */
    FORCEINLINE void SetIdentity()
    {
        Rotation = TQuat<T>::Identity;
        Translation = TVector<T>::ZeroVector;
        Scale3D = TVector<T>::OneVector;
    }

public:
    // ========================================================================
    // Transform Operations
    // ========================================================================

    /**
     * Transform a position (applies scale, rotation, then translation)
     * @param V The position to transform
     * @return The transformed position
     */
    MR_NODISCARD FORCEINLINE TVector<T> TransformPosition(const TVector<T>& V) const
    {
        return Rotation.RotateVector(Scale3D * V) + Translation;
    }

    /**
     * Transform a position without scale
     * @param V The position to transform
     * @return The transformed position
     */
    MR_NODISCARD FORCEINLINE TVector<T> TransformPositionNoScale(const TVector<T>& V) const
    {
        return Rotation.RotateVector(V) + Translation;
    }

    /**
     * Transform a direction vector (applies scale and rotation, no translation)
     * @param V The direction to transform
     * @return The transformed direction
     */
    MR_NODISCARD FORCEINLINE TVector<T> TransformVector(const TVector<T>& V) const
    {
        return Rotation.RotateVector(Scale3D * V);
    }

    /**
     * Transform a direction vector without scale
     * @param V The direction to transform
     * @return The transformed direction
     */
    MR_NODISCARD FORCEINLINE TVector<T> TransformVectorNoScale(const TVector<T>& V) const
    {
        return Rotation.RotateVector(V);
    }

    /**
     * Inverse transform a position
     * @param V The position to inverse transform
     * @return The inverse transformed position
     */
    MR_NODISCARD FORCEINLINE TVector<T> InverseTransformPosition(const TVector<T>& V) const
    {
        return Rotation.UnrotateVector(V - Translation) / Scale3D;
    }

    /**
     * Inverse transform a position without scale
     * @param V The position to inverse transform
     * @return The inverse transformed position
     */
    MR_NODISCARD FORCEINLINE TVector<T> InverseTransformPositionNoScale(const TVector<T>& V) const
    {
        return Rotation.UnrotateVector(V - Translation);
    }

    /**
     * Inverse transform a direction vector
     * @param V The direction to inverse transform
     * @return The inverse transformed direction
     */
    MR_NODISCARD FORCEINLINE TVector<T> InverseTransformVector(const TVector<T>& V) const
    {
        return Rotation.UnrotateVector(V) / Scale3D;
    }

    /**
     * Inverse transform a direction vector without scale
     * @param V The direction to inverse transform
     * @return The inverse transformed direction
     */
    MR_NODISCARD FORCEINLINE TVector<T> InverseTransformVectorNoScale(const TVector<T>& V) const
    {
        return Rotation.UnrotateVector(V);
    }

    /**
     * Transform a rotation
     * @param Q The rotation to transform
     * @return The transformed rotation
     */
    MR_NODISCARD FORCEINLINE TQuat<T> TransformRotation(const TQuat<T>& Q) const
    {
        return Rotation * Q;
    }

    /**
     * Inverse transform a rotation
     * @param Q The rotation to inverse transform
     * @return The inverse transformed rotation
     */
    MR_NODISCARD FORCEINLINE TQuat<T> InverseTransformRotation(const TQuat<T>& Q) const
    {
        return Rotation.Inverse() * Q;
    }

public:
    // ========================================================================
    // Composition
    // ========================================================================

    /**
     * Multiply two transforms (compose)
     * Result = this * Other means: first apply Other, then apply this
     */
    MR_NODISCARD TTransform<T> operator*(const TTransform<T>& Other) const
    {
        TTransform<T> Result;

        // Scale = this.Scale * Other.Scale
        Result.Scale3D = Scale3D * Other.Scale3D;

        // Rotation = this.Rotation * Other.Rotation
        Result.Rotation = Rotation * Other.Rotation;

        // Translation = this.Rotation * (this.Scale * Other.Translation) + this.Translation
        Result.Translation = TransformPosition(Other.Translation);

        Result.DiagnosticCheckNaN_All();
        return Result;
    }

    /** Multiply assignment */
    FORCEINLINE TTransform<T>& operator*=(const TTransform<T>& Other)
    {
        *this = *this * Other;
        return *this;
    }

    /**
     * Get the inverse of this transform
     */
    MR_NODISCARD TTransform<T> Inverse() const
    {
        TTransform<T> Result;

        // Inverse scale
        Result.Scale3D = TVector<T>(
            Scale3D.X != T(0) ? T(1) / Scale3D.X : T(0),
            Scale3D.Y != T(0) ? T(1) / Scale3D.Y : T(0),
            Scale3D.Z != T(0) ? T(1) / Scale3D.Z : T(0)
        );

        // Inverse rotation
        Result.Rotation = Rotation.Inverse();

        // Inverse translation
        Result.Translation = Result.Rotation.RotateVector(Result.Scale3D * (-Translation));

        Result.DiagnosticCheckNaN_All();
        return Result;
    }

    /**
     * Get a transform that is the relative transform from this to Other
     * Result * this = Other
     */
    MR_NODISCARD TTransform<T> GetRelativeTransform(const TTransform<T>& Other) const
    {
        return Inverse() * Other;
    }

    /**
     * Get a transform that is the relative transform of Other from this
     * this * Result = Other
     */
    MR_NODISCARD TTransform<T> GetRelativeTransformReverse(const TTransform<T>& Other) const
    {
        return Other * Inverse();
    }

public:
    // ========================================================================
    // Comparison
    // ========================================================================

    /** Equality comparison */
    MR_NODISCARD FORCEINLINE bool operator==(const TTransform<T>& Other) const
    {
        return Rotation == Other.Rotation && 
               Translation == Other.Translation && 
               Scale3D == Other.Scale3D;
    }

    /** Inequality comparison */
    MR_NODISCARD FORCEINLINE bool operator!=(const TTransform<T>& Other) const
    {
        return !(*this == Other);
    }

    /** Check if this is the identity transform */
    MR_NODISCARD FORCEINLINE bool IsIdentity(T Tolerance = MR_KINDA_SMALL_NUMBER) const
    {
        return Rotation.IsIdentity(Tolerance) &&
               Translation.IsNearlyZero(Tolerance) &&
               Scale3D.Equals(TVector<T>::OneVector, Tolerance);
    }

    /** Check if two transforms are equal within tolerance */
    MR_NODISCARD FORCEINLINE bool Equals(const TTransform<T>& Other, T Tolerance = MR_KINDA_SMALL_NUMBER) const
    {
        return Rotation.Equals(Other.Rotation, Tolerance) &&
               Translation.Equals(Other.Translation, Tolerance) &&
               Scale3D.Equals(Other.Scale3D, Tolerance);
    }

    /** Check if the rotation is normalized */
    MR_NODISCARD FORCEINLINE bool IsRotationNormalized() const
    {
        return Rotation.IsNormalized();
    }

    /** Check if scale is uniform */
    MR_NODISCARD FORCEINLINE bool IsUniformScale(T Tolerance = MR_KINDA_SMALL_NUMBER) const
    {
        return Scale3D.AllComponentsEqual(Tolerance);
    }

    /** Check if there is any negative scale */
    MR_NODISCARD FORCEINLINE bool HasNegativeScale() const
    {
        return Scale3D.X < T(0) || Scale3D.Y < T(0) || Scale3D.Z < T(0);
    }

public:
    // ========================================================================
    // Interpolation
    // ========================================================================

    /**
     * Blend two transforms
     * @param A The starting transform
     * @param B The ending transform
     * @param Alpha The blend factor [0, 1]
     */
    MR_NODISCARD static TTransform<T> Blend(const TTransform<T>& A, const TTransform<T>& B, T Alpha)
    {
        return TTransform<T>(
            TQuat<T>::Slerp(A.Rotation, B.Rotation, Alpha),
            A.Translation + (B.Translation - A.Translation) * Alpha,
            A.Scale3D + (B.Scale3D - A.Scale3D) * Alpha
        );
    }

    /**
     * Blend this transform with another
     * @param Other The other transform
     * @param Alpha The blend factor [0, 1]
     */
    MR_NODISCARD FORCEINLINE TTransform<T> BlendWith(const TTransform<T>& Other, T Alpha) const
    {
        return Blend(*this, Other, Alpha);
    }

public:
    // ========================================================================
    // Matrix Conversion
    // ========================================================================

    /** Convert to a 4x4 transformation matrix */
    MR_NODISCARD TMatrix<T> ToMatrixWithScale() const
    {
        TMatrix<T> Result = TMatrix<T>::MakeFromQuat(Rotation);

        // Apply scale
        Result.M[0][0] *= Scale3D.X; Result.M[0][1] *= Scale3D.X; Result.M[0][2] *= Scale3D.X;
        Result.M[1][0] *= Scale3D.Y; Result.M[1][1] *= Scale3D.Y; Result.M[1][2] *= Scale3D.Y;
        Result.M[2][0] *= Scale3D.Z; Result.M[2][1] *= Scale3D.Z; Result.M[2][2] *= Scale3D.Z;

        // Apply translation
        Result.M[3][0] = Translation.X;
        Result.M[3][1] = Translation.Y;
        Result.M[3][2] = Translation.Z;

        return Result;
    }

    /** Convert to a 4x4 transformation matrix without scale */
    MR_NODISCARD TMatrix<T> ToMatrixNoScale() const
    {
        TMatrix<T> Result = TMatrix<T>::MakeFromQuat(Rotation);

        Result.M[3][0] = Translation.X;
        Result.M[3][1] = Translation.Y;
        Result.M[3][2] = Translation.Z;

        return Result;
    }

    /** Set from a 4x4 transformation matrix */
    void SetFromMatrix(const TMatrix<T>& InMatrix)
    {
        // Extract scale
        Scale3D = InMatrix.GetScaleVector();

        // Handle negative scale
        if (InMatrix.Determinant() < T(0))
        {
            Scale3D.X *= T(-1);
        }

        // Extract rotation (remove scale from matrix first)
        TMatrix<T> RotMatrix = InMatrix;
        if (Scale3D.X != T(0)) { RotMatrix.M[0][0] /= Scale3D.X; RotMatrix.M[0][1] /= Scale3D.X; RotMatrix.M[0][2] /= Scale3D.X; }
        if (Scale3D.Y != T(0)) { RotMatrix.M[1][0] /= Scale3D.Y; RotMatrix.M[1][1] /= Scale3D.Y; RotMatrix.M[1][2] /= Scale3D.Y; }
        if (Scale3D.Z != T(0)) { RotMatrix.M[2][0] /= Scale3D.Z; RotMatrix.M[2][1] /= Scale3D.Z; RotMatrix.M[2][2] /= Scale3D.Z; }

        Rotation = TQuat<T>(RotMatrix);

        // Extract translation
        Translation = InMatrix.GetOrigin();

        DiagnosticCheckNaN_All();
    }

public:
    // ========================================================================
    // Direction Vectors
    // ========================================================================

    /** Get the forward direction (X axis after rotation) */
    MR_NODISCARD FORCEINLINE TVector<T> GetForwardVector() const
    {
        return Rotation.GetForwardVector();
    }

    /** Get the right direction (Y axis after rotation) */
    MR_NODISCARD FORCEINLINE TVector<T> GetRightVector() const
    {
        return Rotation.GetRightVector();
    }

    /** Get the up direction (Z axis after rotation) */
    MR_NODISCARD FORCEINLINE TVector<T> GetUpVector() const
    {
        return Rotation.GetUpVector();
    }

    /** Convert to a string representation */
    MR_NODISCARD inline std::string ToString() const
    {
        char Buffer[256];
        snprintf(Buffer, sizeof(Buffer), 
            "Translation: %s, Rotation: %s, Scale: %s",
            Translation.ToString().c_str(),
            Rotation.ToString().c_str(),
            Scale3D.ToString().c_str()
        );
        return std::string(Buffer);
    }
};

// ============================================================================
// Static Constant Definitions
// ============================================================================

template<typename T>
const TTransform<T> TTransform<T>::Identity = TTransform<T>();

} // namespace Math
} // namespace MonsterEngine
