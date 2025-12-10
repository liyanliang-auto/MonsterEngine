/**
 * Common shader definitions and utilities
 * 
 * This file contains common definitions used across all shaders.
 */

#ifndef COMMON_GLSL
#define COMMON_GLSL

// ============================================================================
// Constants
// ============================================================================

#define PI 3.14159265359
#define TWO_PI 6.28318530718
#define HALF_PI 1.57079632679
#define INV_PI 0.31830988618

#define EPSILON 0.0001
#define MAX_FLOAT 3.402823466e+38

// ============================================================================
// Math Utilities
// ============================================================================

/**
 * Saturate value to [0, 1] range
 */
float saturate(float x)
{
    return clamp(x, 0.0, 1.0);
}

vec2 saturate(vec2 x)
{
    return clamp(x, vec2(0.0), vec2(1.0));
}

vec3 saturate(vec3 x)
{
    return clamp(x, vec3(0.0), vec3(1.0));
}

vec4 saturate(vec4 x)
{
    return clamp(x, vec4(0.0), vec4(1.0));
}

/**
 * Linear interpolation
 */
float lerp(float a, float b, float t)
{
    return mix(a, b, t);
}

vec3 lerp(vec3 a, vec3 b, float t)
{
    return mix(a, b, t);
}

vec4 lerp(vec4 a, vec4 b, float t)
{
    return mix(a, b, t);
}

/**
 * Square of a value
 */
float square(float x)
{
    return x * x;
}

vec3 square(vec3 x)
{
    return x * x;
}

/**
 * Pow with safe handling for negative values
 */
float pow_safe(float base, float exp)
{
    return pow(max(abs(base), EPSILON), exp);
}

// ============================================================================
// Color Utilities
// ============================================================================

/**
 * Convert linear color to sRGB
 */
vec3 linearToSRGB(vec3 color)
{
    vec3 lo = color * 12.92;
    vec3 hi = pow(color, vec3(1.0/2.4)) * 1.055 - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), color));
}

/**
 * Convert sRGB color to linear
 */
vec3 sRGBToLinear(vec3 color)
{
    vec3 lo = color / 12.92;
    vec3 hi = pow((color + 0.055) / 1.055, vec3(2.4));
    return mix(lo, hi, step(vec3(0.04045), color));
}

/**
 * Calculate luminance
 */
float luminance(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

// ============================================================================
// Normal Mapping
// ============================================================================

/**
 * Unpack normal from normal map
 */
vec3 unpackNormal(vec3 packedNormal)
{
    return packedNormal * 2.0 - 1.0;
}

/**
 * Pack normal for storage
 */
vec3 packNormal(vec3 normal)
{
    return normal * 0.5 + 0.5;
}

/**
 * Calculate TBN matrix from normal and tangent
 */
mat3 calculateTBN(vec3 normal, vec3 tangent)
{
    vec3 N = normalize(normal);
    vec3 T = normalize(tangent);
    T = normalize(T - dot(T, N) * N); // Re-orthogonalize
    vec3 B = cross(N, T);
    return mat3(T, B, N);
}

#endif // COMMON_GLSL
