// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file Color.h
 * @brief Color types for rendering - FLinearColor and FColor
 * 
 * This file defines color types following UE5's color system:
 * - FLinearColor: 32-bit floating point RGBA in linear space
 * - FColor: 8-bit per channel RGBA in gamma/sRGB space
 */

#include "Core/CoreTypes.h"
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <string>

namespace MonsterEngine
{

// Forward declarations
struct FColor;
struct FLinearColor;

// ============================================================================
// Gamma Space Enum
// ============================================================================

/**
 * Enum for different gamma spaces
 */
enum class EGammaSpace : uint8_t
{
    /** No gamma correction, colors are in linear space */
    Linear,
    /** Simplified sRGB gamma correction, pow(1/2.2) */
    Pow22,
    /** Standard sRGB conversion */
    sRGB,
    /** Invalid gamma space */
    Invalid
};

// ============================================================================
// FLinearColor - Linear space floating point color
// ============================================================================

/**
 * @brief A linear, 32-bit/component floating point RGBA color
 * 
 * Linear color values are used for lighting calculations and HDR rendering.
 * Values are not clamped and can exceed [0,1] range for HDR.
 */
struct FLinearColor
{
public:
    /** Red component */
    float R;
    /** Green component */
    float G;
    /** Blue component */
    float B;
    /** Alpha component */
    float A;

public:
    // ========================================================================
    // Static Constants
    // ========================================================================

    static const FLinearColor White;
    static const FLinearColor Gray;
    static const FLinearColor Black;
    static const FLinearColor Transparent;
    static const FLinearColor Red;
    static const FLinearColor Green;
    static const FLinearColor Blue;
    static const FLinearColor Yellow;
    static const FLinearColor Cyan;
    static const FLinearColor Magenta;
    static const FLinearColor Orange;

    // ========================================================================
    // sRGB Conversion Tables
    // ========================================================================

    /** Lookup table for sRGB to linear conversion */
    static float sRGBToLinearTable[256];

    /** Lookup table for Pow(2.2) conversion */
    static float Pow22OneOver255Table[256];

public:
    // ========================================================================
    // Constructors
    // ========================================================================

    /** Default constructor (no initialization) */
    FLinearColor() = default;

    /** Constructor with explicit RGBA values */
    constexpr FLinearColor(float InR, float InG, float InB, float InA = 1.0f)
        : R(InR), G(InG), B(InB), A(InA)
    {
    }

    /** Constructor from FColor (sRGB to linear conversion) */
    explicit FLinearColor(const FColor& Color);

    /** Constructor from grayscale value */
    explicit constexpr FLinearColor(float Grayscale)
        : R(Grayscale), G(Grayscale), B(Grayscale), A(1.0f)
    {
    }

public:
    // ========================================================================
    // Operators
    // ========================================================================

    /** Component access by index */
    float& operator[](int32_t Index)
    {
        return (&R)[Index];
    }

    const float& operator[](int32_t Index) const
    {
        return (&R)[Index];
    }

    /** Component-wise addition */
    FLinearColor operator+(const FLinearColor& Other) const
    {
        return FLinearColor(R + Other.R, G + Other.G, B + Other.B, A + Other.A);
    }

    FLinearColor& operator+=(const FLinearColor& Other)
    {
        R += Other.R; G += Other.G; B += Other.B; A += Other.A;
        return *this;
    }

    /** Component-wise subtraction */
    FLinearColor operator-(const FLinearColor& Other) const
    {
        return FLinearColor(R - Other.R, G - Other.G, B - Other.B, A - Other.A);
    }

    FLinearColor& operator-=(const FLinearColor& Other)
    {
        R -= Other.R; G -= Other.G; B -= Other.B; A -= Other.A;
        return *this;
    }

    /** Component-wise multiplication */
    FLinearColor operator*(const FLinearColor& Other) const
    {
        return FLinearColor(R * Other.R, G * Other.G, B * Other.B, A * Other.A);
    }

    FLinearColor& operator*=(const FLinearColor& Other)
    {
        R *= Other.R; G *= Other.G; B *= Other.B; A *= Other.A;
        return *this;
    }

    /** Scalar multiplication */
    FLinearColor operator*(float Scalar) const
    {
        return FLinearColor(R * Scalar, G * Scalar, B * Scalar, A * Scalar);
    }

    FLinearColor& operator*=(float Scalar)
    {
        R *= Scalar; G *= Scalar; B *= Scalar; A *= Scalar;
        return *this;
    }

    /** Component-wise division */
    FLinearColor operator/(const FLinearColor& Other) const
    {
        return FLinearColor(R / Other.R, G / Other.G, B / Other.B, A / Other.A);
    }

    FLinearColor& operator/=(const FLinearColor& Other)
    {
        R /= Other.R; G /= Other.G; B /= Other.B; A /= Other.A;
        return *this;
    }

    /** Scalar division */
    FLinearColor operator/(float Scalar) const
    {
        const float InvScalar = 1.0f / Scalar;
        return FLinearColor(R * InvScalar, G * InvScalar, B * InvScalar, A * InvScalar);
    }

    FLinearColor& operator/=(float Scalar)
    {
        const float InvScalar = 1.0f / Scalar;
        R *= InvScalar; G *= InvScalar; B *= InvScalar; A *= InvScalar;
        return *this;
    }

    /** Equality comparison */
    bool operator==(const FLinearColor& Other) const
    {
        return R == Other.R && G == Other.G && B == Other.B && A == Other.A;
    }

    bool operator!=(const FLinearColor& Other) const
    {
        return !(*this == Other);
    }

    /** Error-tolerant comparison */
    bool Equals(const FLinearColor& Other, float Tolerance = 1.e-4f) const
    {
        return std::abs(R - Other.R) < Tolerance &&
               std::abs(G - Other.G) < Tolerance &&
               std::abs(B - Other.B) < Tolerance &&
               std::abs(A - Other.A) < Tolerance;
    }

public:
    // ========================================================================
    // Conversion Functions
    // ========================================================================

    /**
     * Convert to FColor with optional sRGB conversion
     * @param bSRGB If true, apply sRGB gamma correction
     */
    FColor ToFColor(bool bSRGB) const;

    /**
     * Quantize to FColor with rounding (bypasses sRGB conversion)
     * Matches GPU UNORM<->float conversion spec
     */
    FColor QuantizeRound() const;

    /**
     * Quantize to FColor with floor (bypasses sRGB conversion)
     */
    FColor QuantizeFloor() const;

    /**
     * Convert from sRGB FColor to linear
     */
    static FLinearColor FromSRGBColor(const FColor& Color);

    /**
     * Convert from Pow(1/2.2) FColor to linear
     */
    static FLinearColor FromPow22Color(const FColor& Color);

public:
    // ========================================================================
    // Color Operations
    // ========================================================================

    /**
     * Get clamped color
     * @param InMin Minimum value (default 0)
     * @param InMax Maximum value (default 1)
     */
    FLinearColor GetClamped(float InMin = 0.0f, float InMax = 1.0f) const
    {
        return FLinearColor(
            std::clamp(R, InMin, InMax),
            std::clamp(G, InMin, InMax),
            std::clamp(B, InMin, InMax),
            std::clamp(A, InMin, InMax)
        );
    }

    /** Get a copy with new alpha value */
    FLinearColor CopyWithNewOpacity(float NewOpacity) const
    {
        FLinearColor Result = *this;
        Result.A = NewOpacity;
        return Result;
    }

    /**
     * Compute perceptually weighted luminance
     * Uses Rec. 709 coefficients
     */
    float GetLuminance() const
    {
        return R * 0.2126f + G * 0.7152f + B * 0.0722f;
    }

    /** Get maximum component value */
    float GetMax() const
    {
        return std::max({R, G, B, A});
    }

    /** Get minimum component value */
    float GetMin() const
    {
        return std::min({R, G, B, A});
    }

    /** Check if color is almost black */
    bool IsAlmostBlack() const
    {
        return (R * R) < 1.e-4f && (G * G) < 1.e-4f && (B * B) < 1.e-4f;
    }

    /**
     * Desaturate the color
     * @param Desaturation Amount of desaturation [0..1]
     */
    FLinearColor Desaturate(float Desaturation) const
    {
        float Lum = GetLuminance();
        return FLinearColor(
            R + (Lum - R) * Desaturation,
            G + (Lum - G) * Desaturation,
            B + (Lum - B) * Desaturation,
            A
        );
    }

    /**
     * Convert linear RGB to HSV
     * H is in [0, 360), S and V are in [0, 1]
     */
    FLinearColor LinearRGBToHSV() const;

    /**
     * Convert HSV to linear RGB
     * Assumes H is in [0, 360), S and V are in [0, 1]
     */
    FLinearColor HSVToLinearRGB() const;

    /**
     * Linear interpolation between two colors
     */
    static FLinearColor Lerp(const FLinearColor& A, const FLinearColor& B, float Alpha)
    {
        return FLinearColor(
            A.R + Alpha * (B.R - A.R),
            A.G + Alpha * (B.G - A.G),
            A.B + Alpha * (B.B - A.B),
            A.A + Alpha * (B.A - A.A)
        );
    }

    /**
     * Lerp using HSV color space (takes shortest path for hue)
     */
    static FLinearColor LerpUsingHSV(const FLinearColor& From, const FLinearColor& To, float Progress);

    /**
     * Euclidean distance between two colors
     */
    static float Dist(const FLinearColor& V1, const FLinearColor& V2)
    {
        return std::sqrt(
            (V2.R - V1.R) * (V2.R - V1.R) +
            (V2.G - V1.G) * (V2.G - V1.G) +
            (V2.B - V1.B) * (V2.B - V1.B) +
            (V2.A - V1.A) * (V2.A - V1.A)
        );
    }

public:
    // ========================================================================
    // Factory Functions
    // ========================================================================

    /** Create from HSV values (H in [0,360), S,V in [0,1]) */
    static FLinearColor MakeFromHSV(float H, float S, float V);

    /** Create from 8-bit HSV values */
    static FLinearColor MakeFromHSV8(uint8_t H, uint8_t S, uint8_t V);

    /** Create a random color */
    static FLinearColor MakeRandomColor();

    /** Create a random color from seed */
    static FLinearColor MakeRandomSeededColor(int32_t Seed);

    /** Create from color temperature in Kelvin */
    static FLinearColor MakeFromColorTemperature(float Temp);

public:
    // ========================================================================
    // String Conversion
    // ========================================================================

    /** Convert to string representation */
    std::string ToString() const;

    /** Initialize from string (expects R=,G=,B=,A= format) */
    bool InitFromString(const std::string& InSourceString);
};

// Non-member operator
inline FLinearColor operator*(float Scalar, const FLinearColor& Color)
{
    return Color * Scalar;
}

// ============================================================================
// FColor - 8-bit per channel sRGB color
// ============================================================================

/**
 * @brief Stores a color with 8 bits of precision per channel
 * 
 * Note: Linear color values should always be converted to gamma space
 * before stored in an FColor, as 8 bits of precision is not enough
 * to store linear space colors accurately.
 */
struct FColor
{
public:
    // Platform-specific byte order for BGRA storage
#if defined(_WIN32) || defined(__LITTLE_ENDIAN__)
    union
    {
        struct { uint8_t B, G, R, A; };
        uint32_t Bits;
    };
#else
    union
    {
        struct { uint8_t A, R, G, B; };
        uint32_t Bits;
    };
#endif

public:
    // ========================================================================
    // Static Constants
    // ========================================================================

    static const FColor White;
    static const FColor Black;
    static const FColor Transparent;
    static const FColor Red;
    static const FColor Green;
    static const FColor Blue;
    static const FColor Yellow;
    static const FColor Cyan;
    static const FColor Magenta;
    static const FColor Orange;
    static const FColor Purple;
    static const FColor Turquoise;
    static const FColor Silver;
    static const FColor Emerald;

public:
    // ========================================================================
    // Constructors
    // ========================================================================

    /** Default constructor (no initialization) */
    FColor() = default;

    /** Constructor with explicit RGBA values */
    constexpr FColor(uint8_t InR, uint8_t InG, uint8_t InB, uint8_t InA = 255)
#if defined(_WIN32) || defined(__LITTLE_ENDIAN__)
        : B(InB), G(InG), R(InR), A(InA)
#else
        : A(InA), R(InR), G(InG), B(InB)
#endif
    {
    }

    /** Constructor from packed 32-bit value */
    explicit FColor(uint32_t InColor)
    {
        Bits = InColor;
    }

public:
    // ========================================================================
    // Accessors
    // ========================================================================

    /** Get/Set as 32-bit value */
    uint32_t& DWColor() { return Bits; }
    const uint32_t& DWColor() const { return Bits; }

public:
    // ========================================================================
    // Operators
    // ========================================================================

    /** Equality comparison */
    bool operator==(const FColor& Other) const
    {
        return Bits == Other.Bits;
    }

    bool operator!=(const FColor& Other) const
    {
        return Bits != Other.Bits;
    }

    /** Addition with saturation */
    void operator+=(const FColor& Other)
    {
        R = static_cast<uint8_t>(std::min(static_cast<int>(R) + static_cast<int>(Other.R), 255));
        G = static_cast<uint8_t>(std::min(static_cast<int>(G) + static_cast<int>(Other.G), 255));
        B = static_cast<uint8_t>(std::min(static_cast<int>(B) + static_cast<int>(Other.B), 255));
        A = static_cast<uint8_t>(std::min(static_cast<int>(A) + static_cast<int>(Other.A), 255));
    }

public:
    // ========================================================================
    // Conversion Functions
    // ========================================================================

    /**
     * Reinterpret as linear color (simple division by 255)
     * This is the correct dequantizer for QuantizeRound
     */
    FLinearColor ReinterpretAsLinear() const
    {
        return FLinearColor(
            R / 255.0f,
            G / 255.0f,
            B / 255.0f,
            A / 255.0f
        );
    }

    /**
     * Convert to linear color with sRGB gamma correction
     */
    FLinearColor ToLinearColor() const;

    /** Get a copy with new alpha value */
    FColor WithAlpha(uint8_t Alpha) const
    {
        return FColor(R, G, B, Alpha);
    }

public:
    // ========================================================================
    // Packed Format Conversions
    // ========================================================================

    /** Get as packed ARGB */
    uint32_t ToPackedARGB() const
    {
        return (static_cast<uint32_t>(A) << 24) |
               (static_cast<uint32_t>(R) << 16) |
               (static_cast<uint32_t>(G) << 8) |
               (static_cast<uint32_t>(B) << 0);
    }

    /** Get as packed ABGR */
    uint32_t ToPackedABGR() const
    {
        return (static_cast<uint32_t>(A) << 24) |
               (static_cast<uint32_t>(B) << 16) |
               (static_cast<uint32_t>(G) << 8) |
               (static_cast<uint32_t>(R) << 0);
    }

    /** Get as packed RGBA */
    uint32_t ToPackedRGBA() const
    {
        return (static_cast<uint32_t>(R) << 24) |
               (static_cast<uint32_t>(G) << 16) |
               (static_cast<uint32_t>(B) << 8) |
               (static_cast<uint32_t>(A) << 0);
    }

    /** Get as packed BGRA */
    uint32_t ToPackedBGRA() const
    {
        return (static_cast<uint32_t>(B) << 24) |
               (static_cast<uint32_t>(G) << 16) |
               (static_cast<uint32_t>(R) << 8) |
               (static_cast<uint32_t>(A) << 0);
    }

public:
    // ========================================================================
    // Factory Functions
    // ========================================================================

    /**
     * Create from hexadecimal string
     * Supports: RGB, RRGGBB, RRGGBBAA, #RGB, #RRGGBB, #RRGGBBAA
     */
    static FColor FromHex(const std::string& HexString);

    /** Create a random color */
    static FColor MakeRandomColor();

    /** Create a random color from seed */
    static FColor MakeRandomSeededColor(int32_t Seed);

    /** Create red-to-green gradient color from scalar [0,1] */
    static FColor MakeRedToGreenColorFromScalar(float Scalar);

    /** Create from color temperature in Kelvin */
    static FColor MakeFromColorTemperature(float Temp);

public:
    // ========================================================================
    // Quantization Helpers
    // ========================================================================

    /** Quantize float [0,1] to 8-bit with rounding */
    static uint8_t QuantizeUNormFloatTo8(float UnitFloat)
    {
        UnitFloat = std::clamp(UnitFloat, 0.0f, 1.0f);
        return static_cast<uint8_t>(0.5f + UnitFloat * 255.0f);
    }

    /** Quantize float [0,1] to 16-bit with rounding */
    static uint16_t QuantizeUNormFloatTo16(float UnitFloat)
    {
        UnitFloat = std::clamp(UnitFloat, 0.0f, 1.0f);
        return static_cast<uint16_t>(0.5f + UnitFloat * 65535.0f);
    }

    /** Dequantize 8-bit to float [0,1] */
    static float DequantizeUNorm8ToFloat(int Value8)
    {
        return static_cast<float>(Value8) / 255.0f;
    }

    /** Dequantize 16-bit to float [0,1] */
    static float DequantizeUNorm16ToFloat(int Value16)
    {
        return static_cast<float>(Value16) / 65535.0f;
    }

public:
    // ========================================================================
    // String Conversion
    // ========================================================================

    /** Convert to hexadecimal string (RRGGBBAA) */
    std::string ToHex() const;

    /** Convert to string representation */
    std::string ToString() const;

    /** Initialize from string (expects R=,G=,B=,A= format) */
    bool InitFromString(const std::string& InSourceString);
};

// ============================================================================
// Hash Functions
// ============================================================================

inline uint32_t GetTypeHash(const FLinearColor& Color)
{
    // Simple hash combining all components
    uint32_t Hash = 0;
    const uint32_t* Data = reinterpret_cast<const uint32_t*>(&Color);
    for (int i = 0; i < 4; ++i)
    {
        Hash ^= Data[i] + 0x9e3779b9 + (Hash << 6) + (Hash >> 2);
    }
    return Hash;
}

inline uint32_t GetTypeHash(const FColor& Color)
{
    return Color.DWColor();
}

} // namespace MonsterEngine
