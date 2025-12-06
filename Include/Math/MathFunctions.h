// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file MathFunctions.h
 * @brief FMath utility class with static math functions
 * 
 * This file defines the FMath struct containing static utility functions
 * for common mathematical operations. Following UE5's FMath pattern.
 */

#include "MathFwd.h"
#include "MathUtility.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <random>
#include <type_traits>

namespace MonsterEngine
{
namespace Math
{

/**
 * @brief Static math utility class
 * 
 * FMath provides a collection of static mathematical functions for
 * common operations like trigonometry, interpolation, clamping, etc.
 */
struct FMath
{
    // ========================================================================
    // Basic Math Functions
    // ========================================================================

    /** Returns the absolute value */
    template<typename T>
    MR_NODISCARD static CONSTEXPR FORCEINLINE T Abs(const T A)
    {
        return (A >= T(0)) ? A : -A;
    }

    /** Returns the sign of A (-1, 0, or 1) */
    template<typename T>
    MR_NODISCARD static CONSTEXPR FORCEINLINE T Sign(const T A)
    {
        return (A > T(0)) ? T(1) : ((A < T(0)) ? T(-1) : T(0));
    }

    /** Returns the maximum of two values */
    template<typename T>
    MR_NODISCARD static CONSTEXPR FORCEINLINE T Max(const T A, const T B)
    {
        return (A >= B) ? A : B;
    }

    /** Returns the minimum of two values */
    template<typename T>
    MR_NODISCARD static CONSTEXPR FORCEINLINE T Min(const T A, const T B)
    {
        return (A <= B) ? A : B;
    }

    /** Returns the maximum of three values */
    template<typename T>
    MR_NODISCARD static CONSTEXPR FORCEINLINE T Max3(const T A, const T B, const T C)
    {
        return Max(Max(A, B), C);
    }

    /** Returns the minimum of three values */
    template<typename T>
    MR_NODISCARD static CONSTEXPR FORCEINLINE T Min3(const T A, const T B, const T C)
    {
        return Min(Min(A, B), C);
    }

    /** Returns the index of the maximum of three values (0, 1, or 2) */
    template<typename T>
    MR_NODISCARD static CONSTEXPR FORCEINLINE int32_t Max3Index(const T A, const T B, const T C)
    {
        return (A > B) ? ((A > C) ? 0 : 2) : ((B > C) ? 1 : 2);
    }

    /** Returns the index of the minimum of three values (0, 1, or 2) */
    template<typename T>
    MR_NODISCARD static CONSTEXPR FORCEINLINE int32_t Min3Index(const T A, const T B, const T C)
    {
        return (A < B) ? ((A < C) ? 0 : 2) : ((B < C) ? 1 : 2);
    }

    /** Returns the square of a value */
    template<typename T>
    MR_NODISCARD static CONSTEXPR FORCEINLINE T Square(const T A)
    {
        return A * A;
    }

    /** Returns the cube of a value */
    template<typename T>
    MR_NODISCARD static CONSTEXPR FORCEINLINE T Cube(const T A)
    {
        return A * A * A;
    }

    /** Clamps a value between Min and Max */
    template<typename T>
    MR_NODISCARD static CONSTEXPR FORCEINLINE T Clamp(const T X, const T MinVal, const T MaxVal)
    {
        return (X < MinVal) ? MinVal : ((X > MaxVal) ? MaxVal : X);
    }

    /** Clamps a value between 0 and 1 */
    template<typename T>
    MR_NODISCARD static CONSTEXPR FORCEINLINE T Clamp01(const T X)
    {
        return Clamp(X, T(0), T(1));
    }

    /** Wraps a value to be within [Min, Max] range */
    template<typename T>
    MR_NODISCARD static CONSTEXPR T Wrap(T X, const T Min, const T Max)
    {
        T Size = Max - Min;
        if (Size == T(0))
        {
            return Max;
        }

        while (X < Min)
        {
            X += Size;
        }
        while (X > Max)
        {
            X -= Size;
        }
        return X;
    }

    /** Snaps a value to the nearest grid multiple */
    template<typename T>
    MR_NODISCARD static CONSTEXPR FORCEINLINE T GridSnap(T Location, T Grid)
    {
        return (Grid == T(0)) ? Location : (Floor((Location + Grid / T(2)) / Grid) * Grid);
    }

    // ========================================================================
    // Power and Root Functions
    // ========================================================================

    /** Returns the square root */
    MR_NODISCARD static FORCEINLINE float Sqrt(float Value)
    {
        return std::sqrt(Value);
    }

    MR_NODISCARD static FORCEINLINE double Sqrt(double Value)
    {
        return std::sqrt(Value);
    }

    /** Returns the inverse square root (1/sqrt) */
    MR_NODISCARD static FORCEINLINE float InvSqrt(float Value)
    {
        return 1.0f / std::sqrt(Value);
    }

    MR_NODISCARD static FORCEINLINE double InvSqrt(double Value)
    {
        return 1.0 / std::sqrt(Value);
    }

    /** Returns X raised to the power of Y */
    MR_NODISCARD static FORCEINLINE float Pow(float X, float Y)
    {
        return std::pow(X, Y);
    }

    MR_NODISCARD static FORCEINLINE double Pow(double X, double Y)
    {
        return std::pow(X, Y);
    }

    /** Returns e raised to the power of X */
    MR_NODISCARD static FORCEINLINE float Exp(float X)
    {
        return std::exp(X);
    }

    MR_NODISCARD static FORCEINLINE double Exp(double X)
    {
        return std::exp(X);
    }

    /** Returns 2 raised to the power of X */
    MR_NODISCARD static FORCEINLINE float Exp2(float X)
    {
        return std::exp2(X);
    }

    MR_NODISCARD static FORCEINLINE double Exp2(double X)
    {
        return std::exp2(X);
    }

    /** Returns the natural logarithm of X */
    MR_NODISCARD static FORCEINLINE float Loge(float X)
    {
        return std::log(X);
    }

    MR_NODISCARD static FORCEINLINE double Loge(double X)
    {
        return std::log(X);
    }

    /** Returns the base-2 logarithm of X */
    MR_NODISCARD static FORCEINLINE float Log2(float X)
    {
        return std::log2(X);
    }

    MR_NODISCARD static FORCEINLINE double Log2(double X)
    {
        return std::log2(X);
    }

    /** Returns the base-10 logarithm of X */
    MR_NODISCARD static FORCEINLINE float Log10(float X)
    {
        return std::log10(X);
    }

    MR_NODISCARD static FORCEINLINE double Log10(double X)
    {
        return std::log10(X);
    }

    /** Returns the logarithm of X in the specified base */
    template<typename T>
    MR_NODISCARD static FORCEINLINE T LogX(T Base, T Value)
    {
        return Loge(Value) / Loge(Base);
    }

    // ========================================================================
    // Trigonometric Functions (radians)
    // ========================================================================

    /** Returns the sine of X (radians) */
    MR_NODISCARD static FORCEINLINE float Sin(float X)
    {
        return std::sin(X);
    }

    MR_NODISCARD static FORCEINLINE double Sin(double X)
    {
        return std::sin(X);
    }

    /** Returns the cosine of X (radians) */
    MR_NODISCARD static FORCEINLINE float Cos(float X)
    {
        return std::cos(X);
    }

    MR_NODISCARD static FORCEINLINE double Cos(double X)
    {
        return std::cos(X);
    }

    /** Returns the tangent of X (radians) */
    MR_NODISCARD static FORCEINLINE float Tan(float X)
    {
        return std::tan(X);
    }

    MR_NODISCARD static FORCEINLINE double Tan(double X)
    {
        return std::tan(X);
    }

    /** Returns the arc sine of X (result in radians) */
    MR_NODISCARD static FORCEINLINE float Asin(float X)
    {
        return std::asin(Clamp(X, -1.0f, 1.0f));
    }

    MR_NODISCARD static FORCEINLINE double Asin(double X)
    {
        return std::asin(Clamp(X, -1.0, 1.0));
    }

    /** Returns the arc cosine of X (result in radians) */
    MR_NODISCARD static FORCEINLINE float Acos(float X)
    {
        return std::acos(Clamp(X, -1.0f, 1.0f));
    }

    MR_NODISCARD static FORCEINLINE double Acos(double X)
    {
        return std::acos(Clamp(X, -1.0, 1.0));
    }

    /** Returns the arc tangent of X (result in radians) */
    MR_NODISCARD static FORCEINLINE float Atan(float X)
    {
        return std::atan(X);
    }

    MR_NODISCARD static FORCEINLINE double Atan(double X)
    {
        return std::atan(X);
    }

    /** Returns the arc tangent of Y/X (result in radians, handles quadrants) */
    MR_NODISCARD static FORCEINLINE float Atan2(float Y, float X)
    {
        return std::atan2(Y, X);
    }

    MR_NODISCARD static FORCEINLINE double Atan2(double Y, double X)
    {
        return std::atan2(Y, X);
    }

    /** Computes both sine and cosine of X (radians) */
    template<typename T>
    static FORCEINLINE void SinCos(T* OutSin, T* OutCos, T Value)
    {
        *OutSin = Sin(Value);
        *OutCos = Cos(Value);
    }

    // ========================================================================
    // Angle Conversion
    // ========================================================================

    /** Converts radians to degrees */
    template<typename T>
    MR_NODISCARD static CONSTEXPR FORCEINLINE T RadiansToDegrees(T RadVal)
    {
        return RadVal * (T(180) / T(MR_PI));
    }

    /** Converts degrees to radians */
    template<typename T>
    MR_NODISCARD static CONSTEXPR FORCEINLINE T DegreesToRadians(T DegVal)
    {
        return DegVal * (T(MR_PI) / T(180));
    }

    /** Clamps an angle to [0, 360) degrees */
    template<typename T>
    MR_NODISCARD static T ClampAxis(T Angle)
    {
        Angle = std::fmod(Angle, T(360));
        if (Angle < T(0))
        {
            Angle += T(360);
        }
        return Angle;
    }

    /** Normalizes an angle to [-180, 180) degrees */
    template<typename T>
    MR_NODISCARD static T NormalizeAxis(T Angle)
    {
        Angle = ClampAxis(Angle);
        if (Angle > T(180))
        {
            Angle -= T(360);
        }
        return Angle;
    }

    /** Unwinds an angle in radians to [-PI, PI] */
    template<typename T>
    MR_NODISCARD static CONSTEXPR T UnwindRadians(T A)
    {
        while (A > T(MR_PI))
        {
            A -= T(MR_TWO_PI);
        }
        while (A < T(-MR_PI))
        {
            A += T(MR_TWO_PI);
        }
        return A;
    }

    /** Unwinds an angle in degrees to [-180, 180] */
    template<typename T>
    MR_NODISCARD static CONSTEXPR T UnwindDegrees(T A)
    {
        while (A > T(180))
        {
            A -= T(360);
        }
        while (A < T(-180))
        {
            A += T(360);
        }
        return A;
    }

    /** Find the shortest rotation between two angles in degrees */
    template<typename T>
    MR_NODISCARD static CONSTEXPR T FindDeltaAngleDegrees(T A1, T A2)
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

    /** Find the shortest rotation between two angles in radians */
    template<typename T>
    MR_NODISCARD static CONSTEXPR T FindDeltaAngleRadians(T A1, T A2)
    {
        T Delta = A2 - A1;
        if (Delta > T(MR_PI))
        {
            Delta -= T(MR_TWO_PI);
        }
        else if (Delta < T(-MR_PI))
        {
            Delta += T(MR_TWO_PI);
        }
        return Delta;
    }

    // ========================================================================
    // Rounding Functions
    // ========================================================================

    /** Returns the largest integer less than or equal to X */
    MR_NODISCARD static FORCEINLINE float Floor(float X)
    {
        return std::floor(X);
    }

    MR_NODISCARD static FORCEINLINE double Floor(double X)
    {
        return std::floor(X);
    }

    /** Returns the smallest integer greater than or equal to X */
    MR_NODISCARD static FORCEINLINE float Ceil(float X)
    {
        return std::ceil(X);
    }

    MR_NODISCARD static FORCEINLINE double Ceil(double X)
    {
        return std::ceil(X);
    }

    /** Returns the nearest integer to X (rounds half away from zero) */
    MR_NODISCARD static FORCEINLINE float Round(float X)
    {
        return std::round(X);
    }

    MR_NODISCARD static FORCEINLINE double Round(double X)
    {
        return std::round(X);
    }

    /** Truncates X towards zero */
    MR_NODISCARD static FORCEINLINE float Trunc(float X)
    {
        return std::trunc(X);
    }

    MR_NODISCARD static FORCEINLINE double Trunc(double X)
    {
        return std::trunc(X);
    }

    /** Returns the fractional part of X */
    MR_NODISCARD static FORCEINLINE float Frac(float X)
    {
        return X - Floor(X);
    }

    MR_NODISCARD static FORCEINLINE double Frac(double X)
    {
        return X - Floor(X);
    }

    /** Returns the floating-point remainder of X/Y */
    MR_NODISCARD static FORCEINLINE float Fmod(float X, float Y)
    {
        return std::fmod(X, Y);
    }

    MR_NODISCARD static FORCEINLINE double Fmod(double X, double Y)
    {
        return std::fmod(X, Y);
    }

    /** Truncate to integer */
    MR_NODISCARD static FORCEINLINE int32_t TruncToInt(float X)
    {
        return static_cast<int32_t>(X);
    }

    MR_NODISCARD static FORCEINLINE int64_t TruncToInt64(double X)
    {
        return static_cast<int64_t>(X);
    }

    /** Floor to integer */
    MR_NODISCARD static FORCEINLINE int32_t FloorToInt(float X)
    {
        return static_cast<int32_t>(Floor(X));
    }

    MR_NODISCARD static FORCEINLINE int64_t FloorToInt64(double X)
    {
        return static_cast<int64_t>(Floor(X));
    }

    /** Ceil to integer */
    MR_NODISCARD static FORCEINLINE int32_t CeilToInt(float X)
    {
        return static_cast<int32_t>(Ceil(X));
    }

    MR_NODISCARD static FORCEINLINE int64_t CeilToInt64(double X)
    {
        return static_cast<int64_t>(Ceil(X));
    }

    /** Round to integer */
    MR_NODISCARD static FORCEINLINE int32_t RoundToInt(float X)
    {
        return static_cast<int32_t>(Round(X));
    }

    MR_NODISCARD static FORCEINLINE int64_t RoundToInt64(double X)
    {
        return static_cast<int64_t>(Round(X));
    }

    // ========================================================================
    // Interpolation Functions
    // ========================================================================

    /** Linear interpolation between A and B */
    template<typename T, typename U>
    MR_NODISCARD static CONSTEXPR FORCEINLINE T Lerp(const T& A, const T& B, const U& Alpha)
    {
        return static_cast<T>(A + Alpha * (B - A));
    }

    /** Linear interpolation with clamped alpha */
    template<typename T, typename U>
    MR_NODISCARD static CONSTEXPR FORCEINLINE T LerpStable(const T& A, const T& B, const U& Alpha)
    {
        return Lerp(A, B, Clamp(Alpha, U(0), U(1)));
    }

    /** Inverse linear interpolation - returns alpha given value */
    template<typename T>
    MR_NODISCARD static CONSTEXPR FORCEINLINE T InverseLerp(const T& A, const T& B, const T& Value)
    {
        return (B != A) ? (Value - A) / (B - A) : T(0);
    }

    /** Bi-linear interpolation */
    template<typename T>
    MR_NODISCARD static CONSTEXPR FORCEINLINE T BiLerp(const T& P00, const T& P10, const T& P01, const T& P11, float FracX, float FracY)
    {
        return Lerp(Lerp(P00, P10, FracX), Lerp(P01, P11, FracX), FracY);
    }

    /** Cubic interpolation */
    template<typename T>
    MR_NODISCARD static T CubicInterp(const T& P0, const T& T0, const T& P1, const T& T1, float A)
    {
        const float A2 = A * A;
        const float A3 = A2 * A;
        return static_cast<T>((2 * A3 - 3 * A2 + 1) * P0 + (A3 - 2 * A2 + A) * T0 + (A3 - A2) * T1 + (-2 * A3 + 3 * A2) * P1);
    }

    /** Smooth step interpolation (3x^2 - 2x^3) */
    template<typename T>
    MR_NODISCARD static CONSTEXPR FORCEINLINE T SmoothStep(T A, T B, T X)
    {
        if (X < A)
        {
            return T(0);
        }
        else if (X >= B)
        {
            return T(1);
        }
        T InterpFraction = (X - A) / (B - A);
        return InterpFraction * InterpFraction * (T(3) - T(2) * InterpFraction);
    }

    /** Smoother step interpolation (6x^5 - 15x^4 + 10x^3) */
    template<typename T>
    MR_NODISCARD static CONSTEXPR FORCEINLINE T SmootherStep(T A, T B, T X)
    {
        if (X < A)
        {
            return T(0);
        }
        else if (X >= B)
        {
            return T(1);
        }
        T InterpFraction = (X - A) / (B - A);
        return InterpFraction * InterpFraction * InterpFraction * (InterpFraction * (InterpFraction * T(6) - T(15)) + T(10));
    }

    /** Ease in interpolation */
    template<typename T>
    MR_NODISCARD static FORCEINLINE T InterpEaseIn(T A, T B, T Alpha, T Exp)
    {
        return Lerp(A, B, Pow(Alpha, Exp));
    }

    /** Ease out interpolation */
    template<typename T>
    MR_NODISCARD static FORCEINLINE T InterpEaseOut(T A, T B, T Alpha, T Exp)
    {
        return Lerp(A, B, T(1) - Pow(T(1) - Alpha, Exp));
    }

    /** Ease in/out interpolation */
    template<typename T>
    MR_NODISCARD static FORCEINLINE T InterpEaseInOut(T A, T B, T Alpha, T Exp)
    {
        return Lerp(A, B, (Alpha < T(0.5)) ? 
            InterpEaseIn(T(0), T(1), Alpha * T(2), Exp) * T(0.5) : 
            InterpEaseOut(T(0), T(1), Alpha * T(2) - T(1), Exp) * T(0.5) + T(0.5));
    }

    /** Sine ease in interpolation */
    template<typename T>
    MR_NODISCARD static FORCEINLINE T InterpSinIn(T A, T B, T Alpha)
    {
        return Lerp(A, B, T(1) - Cos(Alpha * T(MR_HALF_PI)));
    }

    /** Sine ease out interpolation */
    template<typename T>
    MR_NODISCARD static FORCEINLINE T InterpSinOut(T A, T B, T Alpha)
    {
        return Lerp(A, B, Sin(Alpha * T(MR_HALF_PI)));
    }

    /** Sine ease in/out interpolation */
    template<typename T>
    MR_NODISCARD static FORCEINLINE T InterpSinInOut(T A, T B, T Alpha)
    {
        return Lerp(A, B, (T(1) - Cos(Alpha * T(MR_PI))) * T(0.5));
    }

    /** Exponential ease in interpolation */
    template<typename T>
    MR_NODISCARD static FORCEINLINE T InterpExpoIn(T A, T B, T Alpha)
    {
        return Lerp(A, B, (Alpha == T(0)) ? T(0) : Pow(T(2), T(10) * (Alpha - T(1))));
    }

    /** Exponential ease out interpolation */
    template<typename T>
    MR_NODISCARD static FORCEINLINE T InterpExpoOut(T A, T B, T Alpha)
    {
        return Lerp(A, B, (Alpha == T(1)) ? T(1) : -Pow(T(2), T(-10) * Alpha) + T(1));
    }

    /** Exponential ease in/out interpolation */
    template<typename T>
    MR_NODISCARD static FORCEINLINE T InterpExpoInOut(T A, T B, T Alpha)
    {
        return (Alpha < T(0.5)) ? 
            InterpExpoIn(A, Lerp(A, B, T(0.5)), Alpha * T(2)) : 
            InterpExpoOut(Lerp(A, B, T(0.5)), B, Alpha * T(2) - T(1));
    }

    /** Circular ease in interpolation */
    template<typename T>
    MR_NODISCARD static FORCEINLINE T InterpCircularIn(T A, T B, T Alpha)
    {
        return Lerp(A, B, T(1) - Sqrt(T(1) - Alpha * Alpha));
    }

    /** Circular ease out interpolation */
    template<typename T>
    MR_NODISCARD static FORCEINLINE T InterpCircularOut(T A, T B, T Alpha)
    {
        Alpha -= T(1);
        return Lerp(A, B, Sqrt(T(1) - Alpha * Alpha));
    }

    /** Circular ease in/out interpolation */
    template<typename T>
    MR_NODISCARD static FORCEINLINE T InterpCircularInOut(T A, T B, T Alpha)
    {
        return (Alpha < T(0.5)) ? 
            InterpCircularIn(A, Lerp(A, B, T(0.5)), Alpha * T(2)) : 
            InterpCircularOut(Lerp(A, B, T(0.5)), B, Alpha * T(2) - T(1));
    }

    // ========================================================================
    // Comparison Functions
    // ========================================================================

    /** Check if two floating point values are nearly equal */
    MR_NODISCARD static FORCEINLINE bool IsNearlyEqual(float A, float B, float ErrorTolerance = MR_KINDA_SMALL_NUMBER)
    {
        return Abs(A - B) <= ErrorTolerance;
    }

    MR_NODISCARD static FORCEINLINE bool IsNearlyEqual(double A, double B, double ErrorTolerance = MR_DOUBLE_KINDA_SMALL_NUMBER)
    {
        return Abs(A - B) <= ErrorTolerance;
    }

    /** Check if a floating point value is nearly zero */
    MR_NODISCARD static FORCEINLINE bool IsNearlyZero(float Value, float ErrorTolerance = MR_SMALL_NUMBER)
    {
        return Abs(Value) <= ErrorTolerance;
    }

    MR_NODISCARD static FORCEINLINE bool IsNearlyZero(double Value, double ErrorTolerance = MR_DOUBLE_SMALL_NUMBER)
    {
        return Abs(Value) <= ErrorTolerance;
    }

    /** Check if a value is a power of two */
    template<typename T>
    MR_NODISCARD static CONSTEXPR FORCEINLINE bool IsPowerOfTwo(T Value)
    {
        return ((Value & (Value - 1)) == T(0));
    }

    /** Check if a value is finite (not NaN or Inf) */
    MR_NODISCARD static FORCEINLINE bool IsFinite(float Value)
    {
        return std::isfinite(Value);
    }

    MR_NODISCARD static FORCEINLINE bool IsFinite(double Value)
    {
        return std::isfinite(Value);
    }

    /** Check if a value is NaN */
    MR_NODISCARD static FORCEINLINE bool IsNaN(float Value)
    {
        return std::isnan(Value);
    }

    MR_NODISCARD static FORCEINLINE bool IsNaN(double Value)
    {
        return std::isnan(Value);
    }

    // ========================================================================
    // Random Number Functions
    // ========================================================================

    /** Returns a random float in [0, 1) */
    MR_NODISCARD static float FRand()
    {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_real_distribution<float> dis(0.0f, 1.0f);
        return dis(gen);
    }

    /** Returns a random double in [0, 1) */
    MR_NODISCARD static double DRand()
    {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        static std::uniform_real_distribution<double> dis(0.0, 1.0);
        return dis(gen);
    }

    /** Returns a random integer in [0, Max) */
    MR_NODISCARD static int32_t RandHelper(int32_t Max)
    {
        return (Max > 0) ? Min(TruncToInt(FRand() * static_cast<float>(Max)), Max - 1) : 0;
    }

    /** Returns a random integer in [Min, Max] */
    MR_NODISCARD static int32_t RandRange(int32_t Min, int32_t Max)
    {
        const int32_t Range = (Max - Min) + 1;
        return Min + RandHelper(Range);
    }

    /** Returns a random float in [Min, Max] */
    MR_NODISCARD static float FRandRange(float Min, float Max)
    {
        return Min + (Max - Min) * FRand();
    }

    /** Returns a random double in [Min, Max] */
    MR_NODISCARD static double DRandRange(double Min, double Max)
    {
        return Min + (Max - Min) * DRand();
    }

    /** Returns a random boolean */
    MR_NODISCARD static bool RandBool()
    {
        return RandHelper(2) == 1;
    }

    // ========================================================================
    // Utility Functions
    // ========================================================================

    /** Divides two integers and rounds up */
    template<typename T>
    MR_NODISCARD static CONSTEXPR FORCEINLINE T DivideAndRoundUp(T Dividend, T Divisor)
    {
        return (Dividend + Divisor - 1) / Divisor;
    }

    /** Divides two integers and rounds down */
    template<typename T>
    MR_NODISCARD static CONSTEXPR FORCEINLINE T DivideAndRoundDown(T Dividend, T Divisor)
    {
        return Dividend / Divisor;
    }

    /** Divides two integers and rounds to nearest */
    template<typename T>
    MR_NODISCARD static CONSTEXPR FORCEINLINE T DivideAndRoundNearest(T Dividend, T Divisor)
    {
        return (Dividend >= 0) ? (Dividend + Divisor / 2) / Divisor : (Dividend - Divisor / 2 + 1) / Divisor;
    }

    /** Map a value from one range to another */
    template<typename T>
    MR_NODISCARD static FORCEINLINE T GetMappedRangeValueClamped(T InRangeA, T InRangeB, T OutRangeA, T OutRangeB, T Value)
    {
        T ClampedPct = Clamp01(InverseLerp(InRangeA, InRangeB, Value));
        return Lerp(OutRangeA, OutRangeB, ClampedPct);
    }

    /** Map a value from one range to another (unclamped) */
    template<typename T>
    MR_NODISCARD static FORCEINLINE T GetMappedRangeValueUnclamped(T InRangeA, T InRangeB, T OutRangeA, T OutRangeB, T Value)
    {
        T Pct = InverseLerp(InRangeA, InRangeB, Value);
        return Lerp(OutRangeA, OutRangeB, Pct);
    }
};

} // namespace Math

// Bring FMath into MonsterEngine namespace
using Math::FMath;

} // namespace MonsterEngine
