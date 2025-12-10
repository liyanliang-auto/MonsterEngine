// Copyright Monster Engine. All Rights Reserved.
// Reference UE5: Engine/Source/Runtime/RenderCore/Public/PackedNormal.h

#pragma once

/**
 * @file PackedNormal.h
 * @brief Packed normal vector types for efficient GPU storage
 * 
 * This file defines packed normal types that compress 3D/4D vectors into
 * compact formats suitable for vertex buffers. Following UE5's approach,
 * we support both 8-bit (FPackedNormal) and 16-bit (FPackedRGBA16N) precision.
 * 
 * The packed formats are:
 * - FPackedNormal: 4 bytes (8-bit per component, RGBA8)
 * - FPackedRGBA16N: 8 bytes (16-bit per component, RGBA16)
 */

#include "Core/CoreTypes.h"
#include "Math/MathFwd.h"
#include "Math/Vector.h"
#include "Math/Vector4.h"

namespace MonsterEngine
{

/**
 * @struct FPackedNormal
 * @brief A packed normal vector using 8-bit components
 * 
 * Stores a normalized vector in 4 bytes (RGBA8 format).
 * Each component is mapped from [-1, 1] to [0, 255].
 * The W component stores the binormal sign for tangent space.
 * 
 * Memory layout: [X:8][Y:8][Z:8][W:8] = 32 bits total
 * 
 * Reference UE5: FPackedNormal
 */
struct FPackedNormal
{
    union
    {
        struct
        {
            uint8 X;    // X component packed to [0, 255]
            uint8 Y;    // Y component packed to [0, 255]
            uint8 Z;    // Z component packed to [0, 255]
            uint8 W;    // W component (binormal sign) packed to [0, 255]
        };
        uint32 Packed;  // All components as a single 32-bit value
    } Vector;

    // ========================================================================
    // Constructors
    // ========================================================================

    /**
     * Default constructor - initializes to (0, 0, 1, 1) which is up vector
     */
    FORCEINLINE FPackedNormal()
    {
        Vector.X = 127;  // 0.0 in normalized space
        Vector.Y = 127;
        Vector.Z = 255;  // 1.0 in normalized space (pointing up)
        Vector.W = 127;  // Positive binormal sign
    }

    /**
     * Constructor from individual components
     * @param InX X component [-1, 1]
     * @param InY Y component [-1, 1]
     * @param InZ Z component [-1, 1]
     * @param InW W component [-1, 1] (default 0 for normal, sign for tangent)
     */
    FORCEINLINE FPackedNormal(float InX, float InY, float InZ, float InW = 0.0f)
    {
        Set(InX, InY, InZ, InW);
    }

    /**
     * Constructor from FVector3f
     * @param InVector The vector to pack (should be normalized)
     */
    FORCEINLINE FPackedNormal(const Math::FVector3f& InVector)
    {
        Set(InVector.X, InVector.Y, InVector.Z, 0.0f);
    }

    /**
     * Constructor from FVector (double precision, will be converted to float)
     * @param InVector The vector to pack (should be normalized)
     */
    FORCEINLINE FPackedNormal(const Math::FVector& InVector)
    {
        Set(static_cast<float>(InVector.X), 
            static_cast<float>(InVector.Y), 
            static_cast<float>(InVector.Z), 
            0.0f);
    }

    /**
     * Constructor from FVector4f
     * @param InVector The 4D vector to pack
     */
    FORCEINLINE FPackedNormal(const Math::FVector4f& InVector)
    {
        Set(InVector.X, InVector.Y, InVector.Z, InVector.W);
    }

    /**
     * Constructor from packed uint32
     * @param InPacked The packed value
     */
    explicit FORCEINLINE FPackedNormal(uint32 InPacked)
    {
        Vector.Packed = InPacked;
    }

    // ========================================================================
    // Setters
    // ========================================================================

    /**
     * Set all components from float values
     * @param InX X component [-1, 1]
     * @param InY Y component [-1, 1]
     * @param InZ Z component [-1, 1]
     * @param InW W component [-1, 1]
     */
    FORCEINLINE void Set(float InX, float InY, float InZ, float InW = 0.0f)
    {
        // Map from [-1, 1] to [0, 255]
        // Formula: packed = (value + 1) * 127.5
        // This maps -1 -> 0, 0 -> 127.5, 1 -> 255
        Vector.X = static_cast<uint8>(Clamp(static_cast<int32>((InX + 1.0f) * 127.5f), 0, 255));
        Vector.Y = static_cast<uint8>(Clamp(static_cast<int32>((InY + 1.0f) * 127.5f), 0, 255));
        Vector.Z = static_cast<uint8>(Clamp(static_cast<int32>((InZ + 1.0f) * 127.5f), 0, 255));
        Vector.W = static_cast<uint8>(Clamp(static_cast<int32>((InW + 1.0f) * 127.5f), 0, 255));
    }

    // ========================================================================
    // Getters / Conversion
    // ========================================================================

    /**
     * Convert to FVector3f
     * @return The unpacked 3D vector
     */
    FORCEINLINE Math::FVector3f ToFVector() const
    {
        // Map from [0, 255] back to [-1, 1]
        // Formula: value = (packed / 127.5) - 1
        return Math::FVector3f(
            (static_cast<float>(Vector.X) / 127.5f) - 1.0f,
            (static_cast<float>(Vector.Y) / 127.5f) - 1.0f,
            (static_cast<float>(Vector.Z) / 127.5f) - 1.0f
        );
    }

    /**
     * Convert to FVector4f (includes W component)
     * @return The unpacked 4D vector
     */
    FORCEINLINE Math::FVector4f ToFVector4f() const
    {
        return Math::FVector4f(
            (static_cast<float>(Vector.X) / 127.5f) - 1.0f,
            (static_cast<float>(Vector.Y) / 127.5f) - 1.0f,
            (static_cast<float>(Vector.Z) / 127.5f) - 1.0f,
            (static_cast<float>(Vector.W) / 127.5f) - 1.0f
        );
    }

    /**
     * Get the W component as a float
     * @return W component in [-1, 1] range
     */
    FORCEINLINE float GetW() const
    {
        return (static_cast<float>(Vector.W) / 127.5f) - 1.0f;
    }

    /**
     * Set the W component (binormal sign)
     * @param InW W component [-1, 1]
     */
    FORCEINLINE void SetW(float InW)
    {
        Vector.W = static_cast<uint8>(Clamp(static_cast<int32>((InW + 1.0f) * 127.5f), 0, 255));
    }

    // ========================================================================
    // Operators
    // ========================================================================

    /**
     * Assignment from FVector3f
     */
    FORCEINLINE FPackedNormal& operator=(const Math::FVector3f& InVector)
    {
        Set(InVector.X, InVector.Y, InVector.Z, 0.0f);
        return *this;
    }

    /**
     * Assignment from FVector4f
     */
    FORCEINLINE FPackedNormal& operator=(const Math::FVector4f& InVector)
    {
        Set(InVector.X, InVector.Y, InVector.Z, InVector.W);
        return *this;
    }

    /**
     * Equality comparison
     */
    FORCEINLINE bool operator==(const FPackedNormal& Other) const
    {
        return Vector.Packed == Other.Vector.Packed;
    }

    /**
     * Inequality comparison
     */
    FORCEINLINE bool operator!=(const FPackedNormal& Other) const
    {
        return Vector.Packed != Other.Vector.Packed;
    }

private:
    /**
     * Clamp helper function
     */
    static FORCEINLINE int32 Clamp(int32 Value, int32 Min, int32 Max)
    {
        return (Value < Min) ? Min : ((Value > Max) ? Max : Value);
    }
};

/**
 * @struct FPackedRGBA16N
 * @brief A packed normal vector using 16-bit components (high precision)
 * 
 * Stores a normalized vector in 8 bytes (RGBA16 format).
 * Each component is mapped from [-1, 1] to [0, 65535].
 * Used when higher precision tangent basis is required.
 * 
 * Memory layout: [X:16][Y:16][Z:16][W:16] = 64 bits total
 * 
 * Reference UE5: FPackedRGBA16N
 */
struct FPackedRGBA16N
{
    int16 X;    // X component packed to [-32768, 32767]
    int16 Y;    // Y component packed to [-32768, 32767]
    int16 Z;    // Z component packed to [-32768, 32767]
    int16 W;    // W component packed to [-32768, 32767]

    // ========================================================================
    // Constructors
    // ========================================================================

    /**
     * Default constructor - initializes to (0, 0, 1, 1)
     */
    FORCEINLINE FPackedRGBA16N()
        : X(0), Y(0), Z(32767), W(0)
    {
    }

    /**
     * Constructor from individual float components
     * @param InX X component [-1, 1]
     * @param InY Y component [-1, 1]
     * @param InZ Z component [-1, 1]
     * @param InW W component [-1, 1]
     */
    FORCEINLINE FPackedRGBA16N(float InX, float InY, float InZ, float InW = 0.0f)
    {
        Set(InX, InY, InZ, InW);
    }

    /**
     * Constructor from FVector3f
     * @param InVector The vector to pack
     */
    FORCEINLINE FPackedRGBA16N(const Math::FVector3f& InVector)
    {
        Set(InVector.X, InVector.Y, InVector.Z, 0.0f);
    }

    /**
     * Constructor from FVector4f
     * @param InVector The 4D vector to pack
     */
    FORCEINLINE FPackedRGBA16N(const Math::FVector4f& InVector)
    {
        Set(InVector.X, InVector.Y, InVector.Z, InVector.W);
    }

    // ========================================================================
    // Setters
    // ========================================================================

    /**
     * Set all components from float values
     * @param InX X component [-1, 1]
     * @param InY Y component [-1, 1]
     * @param InZ Z component [-1, 1]
     * @param InW W component [-1, 1]
     */
    FORCEINLINE void Set(float InX, float InY, float InZ, float InW = 0.0f)
    {
        // Map from [-1, 1] to [-32768, 32767]
        // Using signed 16-bit for better precision around zero
        X = static_cast<int16>(Clamp(static_cast<int32>(InX * 32767.0f), -32768, 32767));
        Y = static_cast<int16>(Clamp(static_cast<int32>(InY * 32767.0f), -32768, 32767));
        Z = static_cast<int16>(Clamp(static_cast<int32>(InZ * 32767.0f), -32768, 32767));
        W = static_cast<int16>(Clamp(static_cast<int32>(InW * 32767.0f), -32768, 32767));
    }

    // ========================================================================
    // Getters / Conversion
    // ========================================================================

    /**
     * Convert to FVector3f
     * @return The unpacked 3D vector
     */
    FORCEINLINE Math::FVector3f ToFVector() const
    {
        return Math::FVector3f(
            static_cast<float>(X) / 32767.0f,
            static_cast<float>(Y) / 32767.0f,
            static_cast<float>(Z) / 32767.0f
        );
    }

    /**
     * Convert to FVector4f
     * @return The unpacked 4D vector
     */
    FORCEINLINE Math::FVector4f ToFVector4f() const
    {
        return Math::FVector4f(
            static_cast<float>(X) / 32767.0f,
            static_cast<float>(Y) / 32767.0f,
            static_cast<float>(Z) / 32767.0f,
            static_cast<float>(W) / 32767.0f
        );
    }

    /**
     * Get the W component as a float
     * @return W component in [-1, 1] range
     */
    FORCEINLINE float GetW() const
    {
        return static_cast<float>(W) / 32767.0f;
    }

    // ========================================================================
    // Operators
    // ========================================================================

    /**
     * Assignment from FVector3f
     */
    FORCEINLINE FPackedRGBA16N& operator=(const Math::FVector3f& InVector)
    {
        Set(InVector.X, InVector.Y, InVector.Z, 0.0f);
        return *this;
    }

    /**
     * Assignment from FVector4f
     */
    FORCEINLINE FPackedRGBA16N& operator=(const Math::FVector4f& InVector)
    {
        Set(InVector.X, InVector.Y, InVector.Z, InVector.W);
        return *this;
    }

    /**
     * Equality comparison
     */
    FORCEINLINE bool operator==(const FPackedRGBA16N& Other) const
    {
        return X == Other.X && Y == Other.Y && Z == Other.Z && W == Other.W;
    }

    /**
     * Inequality comparison
     */
    FORCEINLINE bool operator!=(const FPackedRGBA16N& Other) const
    {
        return !(*this == Other);
    }

private:
    /**
     * Clamp helper function
     */
    static FORCEINLINE int32 Clamp(int32 Value, int32 Min, int32 Max)
    {
        return (Value < Min) ? Min : ((Value > Max) ? Max : Value);
    }
};

/**
 * @struct FVector2DHalf
 * @brief A 2D vector using half-precision (16-bit) floats
 * 
 * Used for UV coordinates when memory is more important than precision.
 * Each component is stored as a 16-bit half-precision float.
 * 
 * Memory layout: [X:16][Y:16] = 32 bits total
 * 
 * Reference UE5: FVector2DHalf
 */
struct FVector2DHalf
{
    uint16 X;   // X component as half-float
    uint16 Y;   // Y component as half-float

    // ========================================================================
    // Constructors
    // ========================================================================

    /**
     * Default constructor
     */
    FORCEINLINE FVector2DHalf()
        : X(0), Y(0)
    {
    }

    /**
     * Constructor from float components
     * @param InX X component
     * @param InY Y component
     */
    FORCEINLINE FVector2DHalf(float InX, float InY)
    {
        X = FloatToHalf(InX);
        Y = FloatToHalf(InY);
    }

    /**
     * Constructor from FVector2f
     * @param InVector The 2D vector
     */
    FORCEINLINE FVector2DHalf(const Math::FVector2f& InVector)
    {
        X = FloatToHalf(InVector.X);
        Y = FloatToHalf(InVector.Y);
    }

    // ========================================================================
    // Conversion
    // ========================================================================

    /**
     * Convert to FVector2f
     * @return The full precision 2D vector
     */
    FORCEINLINE Math::FVector2f ToFVector2f() const
    {
        return Math::FVector2f(HalfToFloat(X), HalfToFloat(Y));
    }

    /**
     * Assignment from FVector2f
     */
    FORCEINLINE FVector2DHalf& operator=(const Math::FVector2f& InVector)
    {
        X = FloatToHalf(InVector.X);
        Y = FloatToHalf(InVector.Y);
        return *this;
    }

    /**
     * Equality comparison
     */
    FORCEINLINE bool operator==(const FVector2DHalf& Other) const
    {
        return X == Other.X && Y == Other.Y;
    }

    /**
     * Inequality comparison
     */
    FORCEINLINE bool operator!=(const FVector2DHalf& Other) const
    {
        return !(*this == Other);
    }

private:
    /**
     * Convert float to half-precision float (IEEE 754)
     * @param Value The float value to convert
     * @return The half-precision representation
     */
    static FORCEINLINE uint16 FloatToHalf(float Value)
    {
        // Simple implementation - for production, use hardware intrinsics
        union { float f; uint32 u; } fu;
        fu.f = Value;
        
        uint32 sign = (fu.u >> 16) & 0x8000;
        int32 exponent = ((fu.u >> 23) & 0xFF) - 127 + 15;
        uint32 mantissa = fu.u & 0x7FFFFF;
        
        if (exponent <= 0)
        {
            // Underflow to zero
            return static_cast<uint16>(sign);
        }
        else if (exponent >= 31)
        {
            // Overflow to infinity
            return static_cast<uint16>(sign | 0x7C00);
        }
        
        return static_cast<uint16>(sign | (exponent << 10) | (mantissa >> 13));
    }

    /**
     * Convert half-precision float to float
     * @param Value The half-precision value
     * @return The float representation
     */
    static FORCEINLINE float HalfToFloat(uint16 Value)
    {
        // Simple implementation - for production, use hardware intrinsics
        uint32 sign = (Value & 0x8000) << 16;
        uint32 exponent = (Value >> 10) & 0x1F;
        uint32 mantissa = Value & 0x3FF;
        
        if (exponent == 0)
        {
            // Zero or denormalized
            if (mantissa == 0)
            {
                union { uint32 u; float f; } uf;
                uf.u = sign;
                return uf.f;
            }
            // Denormalized - convert to normalized
            while ((mantissa & 0x400) == 0)
            {
                mantissa <<= 1;
                exponent--;
            }
            exponent++;
            mantissa &= 0x3FF;
        }
        else if (exponent == 31)
        {
            // Infinity or NaN
            union { uint32 u; float f; } uf;
            uf.u = sign | 0x7F800000 | (mantissa << 13);
            return uf.f;
        }
        
        union { uint32 u; float f; } uf;
        uf.u = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
        return uf.f;
    }
};

/**
 * @struct FColor
 * @brief A 32-bit RGBA color
 * 
 * Stores color as 4 bytes (BGRA format for compatibility with most APIs).
 * Each component is in the range [0, 255].
 * 
 * Reference UE5: FColor
 */
struct FColor
{
    union
    {
        struct
        {
            uint8 B;    // Blue component
            uint8 G;    // Green component
            uint8 R;    // Red component
            uint8 A;    // Alpha component
        };
        uint32 DWColor; // All components as a single 32-bit value
    };

    // ========================================================================
    // Constructors
    // ========================================================================

    /**
     * Default constructor - initializes to opaque black
     */
    FORCEINLINE FColor()
        : B(0), G(0), R(0), A(255)
    {
    }

    /**
     * Constructor from individual components
     * @param InR Red component [0, 255]
     * @param InG Green component [0, 255]
     * @param InB Blue component [0, 255]
     * @param InA Alpha component [0, 255]
     */
    FORCEINLINE FColor(uint8 InR, uint8 InG, uint8 InB, uint8 InA = 255)
        : B(InB), G(InG), R(InR), A(InA)
    {
    }

    /**
     * Constructor from packed uint32 (ARGB format)
     * @param InColor The packed color value
     */
    explicit FORCEINLINE FColor(uint32 InColor)
    {
        DWColor = InColor;
    }

    // ========================================================================
    // Predefined Colors
    // ========================================================================

    static const FColor White;
    static const FColor Black;
    static const FColor Red;
    static const FColor Green;
    static const FColor Blue;
    static const FColor Yellow;
    static const FColor Cyan;
    static const FColor Magenta;
    static const FColor Transparent;

    // ========================================================================
    // Conversion
    // ========================================================================

    /**
     * Convert to linear color (FVector4f with values in [0, 1])
     * @return Linear color representation
     */
    FORCEINLINE Math::FVector4f ToLinearColor() const
    {
        return Math::FVector4f(
            static_cast<float>(R) / 255.0f,
            static_cast<float>(G) / 255.0f,
            static_cast<float>(B) / 255.0f,
            static_cast<float>(A) / 255.0f
        );
    }

    /**
     * Create from linear color
     * @param LinearColor Linear color with values in [0, 1]
     * @return FColor representation
     */
    static FORCEINLINE FColor FromLinearColor(const Math::FVector4f& LinearColor)
    {
        return FColor(
            static_cast<uint8>(Clamp(static_cast<int32>(LinearColor.X * 255.0f), 0, 255)),
            static_cast<uint8>(Clamp(static_cast<int32>(LinearColor.Y * 255.0f), 0, 255)),
            static_cast<uint8>(Clamp(static_cast<int32>(LinearColor.Z * 255.0f), 0, 255)),
            static_cast<uint8>(Clamp(static_cast<int32>(LinearColor.W * 255.0f), 0, 255))
        );
    }

    // ========================================================================
    // Operators
    // ========================================================================

    FORCEINLINE bool operator==(const FColor& Other) const
    {
        return DWColor == Other.DWColor;
    }

    FORCEINLINE bool operator!=(const FColor& Other) const
    {
        return DWColor != Other.DWColor;
    }

private:
    static FORCEINLINE int32 Clamp(int32 Value, int32 Min, int32 Max)
    {
        return (Value < Min) ? Min : ((Value > Max) ? Max : Value);
    }
};

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Calculate the binormal sign from tangent basis vectors
 * @param TangentX The tangent vector
 * @param TangentY The binormal vector
 * @param TangentZ The normal vector
 * @return +1 or -1 depending on the handedness of the basis
 */
FORCEINLINE float GetBasisDeterminantSign(
    const Math::FVector3f& TangentX,
    const Math::FVector3f& TangentY,
    const Math::FVector3f& TangentZ)
{
    // Calculate determinant of the tangent basis matrix
    // det = X . (Y x Z)
    Math::FVector3f Cross(
        TangentY.Y * TangentZ.Z - TangentY.Z * TangentZ.Y,
        TangentY.Z * TangentZ.X - TangentY.X * TangentZ.Z,
        TangentY.X * TangentZ.Y - TangentY.Y * TangentZ.X
    );
    
    float Dot = TangentX.X * Cross.X + TangentX.Y * Cross.Y + TangentX.Z * Cross.Z;
    return (Dot < 0.0f) ? -1.0f : 1.0f;
}

/**
 * Calculate the binormal sign and return as a byte for FPackedNormal.W
 * @param TangentX The tangent vector
 * @param TangentY The binormal vector
 * @param TangentZ The normal vector
 * @return 0 for negative, 255 for positive
 */
FORCEINLINE uint8 GetBasisDeterminantSignByte(
    const Math::FVector3f& TangentX,
    const Math::FVector3f& TangentY,
    const Math::FVector3f& TangentZ)
{
    return GetBasisDeterminantSign(TangentX, TangentY, TangentZ) < 0.0f ? 0 : 255;
}

/**
 * Generate Y axis (binormal) from X (tangent) and Z (normal) with sign
 * @param TangentX The tangent vector (packed)
 * @param TangentZ The normal vector with sign in W (packed)
 * @return The computed binormal vector
 */
FORCEINLINE Math::FVector3f GenerateYAxis(
    const FPackedNormal& TangentX,
    const FPackedNormal& TangentZ)
{
    Math::FVector3f X = TangentX.ToFVector();
    Math::FVector4f Z4 = TangentZ.ToFVector4f();
    Math::FVector3f Z(Z4.X, Z4.Y, Z4.Z);
    
    // Y = Z x X * sign
    float Sign = Z4.W < 0.0f ? -1.0f : 1.0f;
    
    return Math::FVector3f(
        (Z.Y * X.Z - Z.Z * X.Y) * Sign,
        (Z.Z * X.X - Z.X * X.Z) * Sign,
        (Z.X * X.Y - Z.Y * X.X) * Sign
    );
}

/**
 * Generate Y axis from high precision packed normals
 */
FORCEINLINE Math::FVector3f GenerateYAxis(
    const FPackedRGBA16N& TangentX,
    const FPackedRGBA16N& TangentZ)
{
    Math::FVector3f X = TangentX.ToFVector();
    Math::FVector4f Z4 = TangentZ.ToFVector4f();
    Math::FVector3f Z(Z4.X, Z4.Y, Z4.Z);
    
    float Sign = Z4.W < 0.0f ? -1.0f : 1.0f;
    
    return Math::FVector3f(
        (Z.Y * X.Z - Z.Z * X.Y) * Sign,
        (Z.Z * X.X - Z.X * X.Z) * Sign,
        (Z.X * X.Y - Z.Y * X.X) * Sign
    );
}

} // namespace MonsterEngine
