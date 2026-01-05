//------------------------------------------------------------------------------
// BRDF.glsl - Physically Based Rendering BRDF Functions
//------------------------------------------------------------------------------
// Copyright Monster Engine. All Rights Reserved.
// Reference: Google Filament, UE5 BRDF implementations
//
// This file contains BRDF (Bidirectional Reflectance Distribution Function)
// implementations for PBR rendering. Supports both Vulkan and OpenGL backends.
//
// Row-major matrices, row vector multiplication (UE5 convention)
// Non-reverse Z depth buffer
//------------------------------------------------------------------------------

#ifndef BRDF_GLSL
#define BRDF_GLSL

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define PI 3.14159265358979323846
#define INV_PI 0.31830988618379067154
#define HALF_PI 1.5707963267948966
#define EPSILON 1e-6

// Minimum roughness to avoid division by zero
#define MIN_ROUGHNESS 0.045
#define MIN_N_DOT_V 1e-4

//------------------------------------------------------------------------------
// Utility Functions
//------------------------------------------------------------------------------

// Clamp value to [0, 1]
float saturate(float x) {
    return clamp(x, 0.0, 1.0);
}

vec3 saturate(vec3 x) {
    return clamp(x, vec3(0.0), vec3(1.0));
}

// Square of a value
float sq(float x) {
    return x * x;
}

// Fifth power (for Schlick approximation)
float pow5(float x) {
    float x2 = x * x;
    return x2 * x2 * x;
}

// Remap roughness to alpha (perceptual roughness squared)
float roughnessToAlpha(float roughness) {
    return roughness * roughness;
}

//------------------------------------------------------------------------------
// Normal Distribution Functions (D)
//------------------------------------------------------------------------------

/**
 * GGX/Trowbridge-Reitz Normal Distribution Function
 * 
 * @param alpha Roughness squared (perceptual roughness^2)
 * @param NoH   Dot product of normal and half vector
 * @return      Distribution value
 * 
 * Reference: Walter et al. 2007, "Microfacet Models for Refraction through Rough Surfaces"
 */
float D_GGX(float alpha, float NoH) {
    float a2 = alpha * alpha;
    float d = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * d * d + EPSILON);
}

/**
 * GGX NDF with improved numerical stability
 * Uses 1 - NoH^2 = sin^2(theta) for better precision near highlights
 * 
 * @param alpha Roughness squared
 * @param NoH   Dot product of normal and half vector
 * @param h     Half vector (for cross product computation)
 * @param n     Normal vector
 * @return      Distribution value
 */
float D_GGX_Stable(float alpha, float NoH, vec3 h, vec3 n) {
    // Use Lagrange's identity for better precision:
    // ||N x H||^2 = 1 - (N.H)^2 when N and H are unit vectors
    vec3 NxH = cross(n, h);
    float oneMinusNoHSquared = dot(NxH, NxH);
    
    float a = NoH * alpha;
    float k = alpha / (oneMinusNoHSquared + a * a);
    float d = k * k * INV_PI;
    return d;
}

//------------------------------------------------------------------------------
// Geometry/Visibility Functions (G/V)
//------------------------------------------------------------------------------

/**
 * Smith GGX Correlated Visibility Function
 * Height-correlated masking-shadowing function
 * 
 * @param alpha Roughness squared
 * @param NoV   Dot product of normal and view direction
 * @param NoL   Dot product of normal and light direction
 * @return      Visibility value (G / (4 * NoV * NoL))
 * 
 * Reference: Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
 */
float V_SmithGGXCorrelated(float alpha, float NoV, float NoL) {
    float a2 = alpha * alpha;
    float lambdaV = NoL * sqrt((NoV - a2 * NoV) * NoV + a2);
    float lambdaL = NoV * sqrt((NoL - a2 * NoL) * NoL + a2);
    float v = 0.5 / (lambdaV + lambdaL + EPSILON);
    return v;
}

/**
 * Fast approximation of Smith GGX Correlated Visibility
 * Good for real-time rendering with acceptable quality
 * 
 * @param alpha Roughness squared
 * @param NoV   Dot product of normal and view direction
 * @param NoL   Dot product of normal and light direction
 * @return      Visibility value
 * 
 * Reference: Hammon 2017, "PBR Diffuse Lighting for GGX+Smith Microsurfaces"
 */
float V_SmithGGXCorrelated_Fast(float alpha, float NoV, float NoL) {
    float v = 0.5 / mix(2.0 * NoL * NoV, NoL + NoV, alpha);
    return v;
}

/**
 * Kelemen Visibility Function
 * Simple and efficient, good for clear coat
 * 
 * @param LoH Dot product of light and half vector
 * @return    Visibility value
 * 
 * Reference: Kelemen 2001, "A Microfacet Based Coupled Specular-Matte BRDF Model"
 */
float V_Kelemen(float LoH) {
    return 0.25 / (LoH * LoH + EPSILON);
}

//------------------------------------------------------------------------------
// Fresnel Functions (F)
//------------------------------------------------------------------------------

/**
 * Schlick Fresnel Approximation
 * 
 * @param f0  Reflectance at normal incidence
 * @param f90 Reflectance at grazing angle (usually 1.0)
 * @param VoH Dot product of view and half vector
 * @return    Fresnel reflectance
 * 
 * Reference: Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"
 */
vec3 F_Schlick(vec3 f0, float f90, float VoH) {
    return f0 + (f90 - f0) * pow5(1.0 - VoH);
}

/**
 * Schlick Fresnel with default f90 = 1.0
 */
vec3 F_Schlick(vec3 f0, float VoH) {
    return f0 + (1.0 - f0) * pow5(1.0 - VoH);
}

/**
 * Scalar Schlick Fresnel
 */
float F_Schlick(float f0, float f90, float VoH) {
    return f0 + (f90 - f0) * pow5(1.0 - VoH);
}

/**
 * Compute f90 from f0 for energy conservation
 * Based on Filament's approach
 */
float computeF90(vec3 f0) {
    return saturate(dot(f0, vec3(50.0 * 0.33)));
}

//------------------------------------------------------------------------------
// Diffuse BRDF Functions
//------------------------------------------------------------------------------

/**
 * Lambertian Diffuse BRDF
 * Simple and efficient, energy conserving
 * 
 * @return Diffuse BRDF value (1/PI)
 */
float Fd_Lambert() {
    return INV_PI;
}

/**
 * Disney/Burley Diffuse BRDF
 * More realistic diffuse with roughness-dependent response
 * 
 * @param roughness Perceptual roughness
 * @param NoV       Dot product of normal and view direction
 * @param NoL       Dot product of normal and light direction
 * @param LoH       Dot product of light and half vector
 * @return          Diffuse BRDF value
 * 
 * Reference: Burley 2012, "Physically-Based Shading at Disney"
 */
float Fd_Burley(float roughness, float NoV, float NoL, float LoH) {
    float f90 = 0.5 + 2.0 * roughness * LoH * LoH;
    float lightScatter = F_Schlick(1.0, f90, NoL);
    float viewScatter = F_Schlick(1.0, f90, NoV);
    return lightScatter * viewScatter * INV_PI;
}

//------------------------------------------------------------------------------
// Complete BRDF Evaluation
//------------------------------------------------------------------------------

/**
 * Evaluate specular BRDF (Cook-Torrance microfacet model)
 * 
 * @param alpha Roughness squared
 * @param f0    Fresnel reflectance at normal incidence
 * @param NoV   Dot product of normal and view direction
 * @param NoL   Dot product of normal and light direction
 * @param NoH   Dot product of normal and half vector
 * @param LoH   Dot product of light and half vector
 * @return      Specular BRDF color
 */
vec3 specularBRDF(float alpha, vec3 f0, float NoV, float NoL, float NoH, float LoH) {
    float D = D_GGX(alpha, NoH);
    float V = V_SmithGGXCorrelated(alpha, NoV, NoL);
    vec3 F = F_Schlick(f0, LoH);
    
    return D * V * F;
}

/**
 * Evaluate diffuse BRDF
 * 
 * @param diffuseColor Diffuse albedo color
 * @param roughness    Perceptual roughness
 * @param NoV          Dot product of normal and view direction
 * @param NoL          Dot product of normal and light direction
 * @param LoH          Dot product of light and half vector
 * @return             Diffuse BRDF color
 */
vec3 diffuseBRDF(vec3 diffuseColor, float roughness, float NoV, float NoL, float LoH) {
    // Use Lambert for performance, Burley for quality
    float Fd = Fd_Lambert();
    return diffuseColor * Fd;
}

//------------------------------------------------------------------------------
// Material Parameter Helpers
//------------------------------------------------------------------------------

/**
 * Compute diffuse color from base color and metallic
 * Metals have no diffuse component
 */
vec3 computeDiffuseColor(vec3 baseColor, float metallic) {
    return baseColor * (1.0 - metallic);
}

/**
 * Compute F0 (specular reflectance at normal incidence)
 * Dielectrics use reflectance parameter, metals use base color
 * 
 * @param baseColor   Base color of material
 * @param metallic    Metallic factor [0, 1]
 * @param reflectance Reflectance for dielectrics (default 0.5 = 4% reflectance)
 */
vec3 computeF0(vec3 baseColor, float metallic, float reflectance) {
    // Dielectric F0 from reflectance: F0 = 0.16 * reflectance^2
    // This maps reflectance 0.5 to F0 = 0.04 (typical dielectric)
    vec3 dielectricF0 = vec3(0.16 * reflectance * reflectance);
    return mix(dielectricF0, baseColor, metallic);
}

/**
 * Remap perceptual roughness to avoid artifacts
 */
float clampRoughness(float roughness) {
    return max(roughness, MIN_ROUGHNESS);
}

#endif // BRDF_GLSL
