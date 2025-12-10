// Copyright Monster Engine. All Rights Reserved.

/**
 * @file LightingCommon.glsl
 * @brief Common lighting functions and structures for GLSL shaders
 * 
 * This file contains the core lighting calculations including:
 * - Light data structures
 * - Attenuation functions
 * - BRDF functions (Lambert, Blinn-Phong, GGX)
 * - Shadow calculations
 * 
 * Reference UE5 files:
 * - Engine/Shaders/Private/DeferredLightingCommon.ush
 * - Engine/Shaders/Private/BRDF.ush
 * - Engine/Shaders/Private/ShadingModels.ush
 */

#ifndef LIGHTING_COMMON_GLSL
#define LIGHTING_COMMON_GLSL

// ============================================================================
// Constants
// ============================================================================

#define PI 3.14159265359
#define INV_PI 0.31830988618
#define HALF_PI 1.57079632679

// Light type constants (must match ELightTypeShader in C++)
#define LIGHT_TYPE_DIRECTIONAL 0
#define LIGHT_TYPE_POINT       1
#define LIGHT_TYPE_SPOT        2
#define LIGHT_TYPE_RECT        3

// Maximum number of local lights
#define MAX_LOCAL_LIGHTS 256

// ============================================================================
// Light Data Structures
// ============================================================================

/**
 * Lightweight light shader parameters
 * Matches FLightShaderParameters in C++
 */
struct LightShaderParameters
{
    vec3  TranslatedWorldPosition;  // World position (camera-relative)
    float InvRadius;                // 1.0 / AttenuationRadius
    
    vec3  Color;                    // Light color * intensity
    float FalloffExponent;          // 0 = inverse squared
    
    vec3  Direction;                // Normalized light direction
    float SpecularScale;            // Specular contribution scale
    
    vec3  Tangent;                  // Tangent for rect lights
    float SourceRadius;             // Area light source radius
    
    vec2  SpotAngles;               // x = cos(inner), y = 1/(cos(inner)-cos(outer))
    float SoftSourceRadius;         // Soft source radius
    float SourceLength;             // Source length for capsule lights
};

/**
 * Directional light parameters
 * Matches FDirectionalLightShaderParameters in C++
 */
struct DirectionalLightData
{
    uint  HasDirectionalLight;
    uint  ShadowMapChannelMask;
    vec2  DistanceFadeMAD;
    
    vec3  Color;
    float Padding0;
    
    vec3  Direction;
    float SourceRadius;
};

/**
 * Deferred light data for full-featured lighting
 * Matches FDeferredLightData in C++
 */
struct DeferredLightData
{
    vec3  TranslatedWorldPosition;
    float InvRadius;
    
    vec3  Color;
    float FalloffExponent;
    
    vec3  Direction;
    float SpecularScale;
    
    vec3  Tangent;
    float SourceRadius;
    
    vec2  SpotAngles;
    float SoftSourceRadius;
    float SourceLength;
    
    float ContactShadowLength;
    float ContactShadowCastingIntensity;
    float ContactShadowNonCastingIntensity;
    bool  bContactShadowLengthInWS;
    
    vec2  DistanceFadeMAD;
    vec4  ShadowMapChannelMask;
    uint  ShadowedBits;
    
    bool  bInverseSquared;
    bool  bRadialLight;
    bool  bSpotLight;
    bool  bRectLight;
};

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Square a value
 */
float Square(float x)
{
    return x * x;
}

/**
 * Pow4 - faster than pow(x, 4)
 */
float Pow4(float x)
{
    float x2 = x * x;
    return x2 * x2;
}

/**
 * Pow5 - faster than pow(x, 5)
 */
float Pow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}

/**
 * Saturate - clamp to [0, 1]
 */
float saturate(float x)
{
    return clamp(x, 0.0, 1.0);
}

vec3 saturate(vec3 x)
{
    return clamp(x, vec3(0.0), vec3(1.0));
}

// ============================================================================
// Attenuation Functions
// ============================================================================

/**
 * Calculate radial attenuation for point/spot lights
 * Uses UE5's physically-based falloff with smooth cutoff
 * 
 * @param DistanceSqr Squared distance from light to surface
 * @param InvRadiusSqr Squared inverse of attenuation radius
 * @return Attenuation factor [0, 1]
 */
float GetDistanceAttenuation(float DistanceSqr, float InvRadiusSqr)
{
    // UE5-style smooth falloff with radius cutoff
    // Attenuation = 1 / (d^2 + 1) * saturate(1 - (d/r)^4)^2
    
    float DistanceAttenuation = 1.0 / (DistanceSqr + 1.0);
    
    // Smooth radius falloff
    float Factor = DistanceSqr * InvRadiusSqr;
    float SmoothFactor = saturate(1.0 - Factor * Factor);
    SmoothFactor = SmoothFactor * SmoothFactor;
    
    return DistanceAttenuation * SmoothFactor;
}

/**
 * Calculate radial attenuation with custom falloff exponent
 * 
 * @param ToLight Vector from surface to light
 * @param InvRadius Inverse of attenuation radius
 * @param FalloffExponent Falloff exponent (0 = inverse squared)
 * @return Attenuation factor [0, 1]
 */
float RadialAttenuation(vec3 ToLight, float InvRadius, float FalloffExponent)
{
    float DistanceSqr = dot(ToLight, ToLight);
    float InvRadiusSqr = InvRadius * InvRadius;
    
    if (FalloffExponent == 0.0)
    {
        // Physically correct inverse squared falloff
        return GetDistanceAttenuation(DistanceSqr, InvRadiusSqr);
    }
    else
    {
        // Custom falloff curve
        float Distance = sqrt(DistanceSqr);
        float NormalizedDistance = Distance * InvRadius;
        float Falloff = pow(saturate(1.0 - Pow4(NormalizedDistance)), FalloffExponent);
        return Falloff / (DistanceSqr + 1.0);
    }
}

/**
 * Calculate spot light cone attenuation
 * 
 * @param L Normalized direction from surface to light
 * @param LightDirection Normalized light direction (direction light is pointing)
 * @param SpotAngles x = cos(inner), y = 1/(cos(inner) - cos(outer))
 * @return Attenuation factor [0, 1]
 */
float SpotAttenuation(vec3 L, vec3 LightDirection, vec2 SpotAngles)
{
    float CosAngle = dot(-L, LightDirection);
    float Attenuation = saturate((CosAngle - SpotAngles.x) * SpotAngles.y);
    return Attenuation * Attenuation;
}

/**
 * Get local light attenuation (point or spot)
 * 
 * @param WorldPos World position of the surface point
 * @param LightData Light parameters
 * @param OutToLight Output: vector from surface to light
 * @param OutL Output: normalized direction to light
 * @return Total attenuation factor
 */
float GetLocalLightAttenuation(
    vec3 WorldPos,
    LightShaderParameters LightData,
    out vec3 OutToLight,
    out vec3 OutL)
{
    OutToLight = LightData.TranslatedWorldPosition - WorldPos;
    float DistanceSqr = dot(OutToLight, OutToLight);
    OutL = OutToLight * inversesqrt(DistanceSqr);
    
    // Distance attenuation
    float LightMask = RadialAttenuation(OutToLight, LightData.InvRadius, LightData.FalloffExponent);
    
    // Spot light cone attenuation
    if (LightData.SpotAngles.y > 0.0)
    {
        LightMask *= SpotAttenuation(OutL, LightData.Direction, LightData.SpotAngles);
    }
    
    return LightMask;
}

// ============================================================================
// BRDF Functions
// ============================================================================

/**
 * Lambert diffuse BRDF
 * 
 * @param DiffuseColor Surface diffuse color
 * @return Diffuse contribution
 */
vec3 Diffuse_Lambert(vec3 DiffuseColor)
{
    return DiffuseColor * INV_PI;
}

/**
 * Blinn-Phong specular distribution
 * 
 * @param Roughness Surface roughness [0, 1]
 * @param NoH Dot product of normal and half vector
 * @return Specular distribution term
 */
float D_BlinnPhong(float Roughness, float NoH)
{
    // Convert roughness to Blinn-Phong exponent
    // Roughness 0 -> exponent ~8192 (very sharp)
    // Roughness 1 -> exponent ~1 (very diffuse)
    float a = max(Roughness * Roughness, 0.001);
    float a2 = a * a;
    float n = 2.0 / a2 - 2.0;
    
    // Normalization factor
    float normalization = (n + 2.0) * INV_PI * 0.5;
    
    return normalization * pow(max(NoH, 0.0), n);
}

/**
 * GGX/Trowbridge-Reitz normal distribution function
 * 
 * @param a2 Roughness squared squared (alpha^2)
 * @param NoH Dot product of normal and half vector
 * @return Distribution term
 */
float D_GGX(float a2, float NoH)
{
    float d = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * d * d);
}

/**
 * Smith-GGX visibility function (approximation)
 * 
 * @param a2 Roughness squared squared
 * @param NoV Dot product of normal and view direction
 * @param NoL Dot product of normal and light direction
 * @return Visibility term
 */
float Vis_SmithJointApprox(float a2, float NoV, float NoL)
{
    float a = sqrt(a2);
    float Vis_SmithV = NoL * (NoV * (1.0 - a) + a);
    float Vis_SmithL = NoV * (NoL * (1.0 - a) + a);
    return 0.5 / (Vis_SmithV + Vis_SmithL + 0.0001);
}

/**
 * Schlick Fresnel approximation
 * 
 * @param SpecularColor Specular color at normal incidence (F0)
 * @param VoH Dot product of view and half vector
 * @return Fresnel term
 */
vec3 F_Schlick(vec3 SpecularColor, float VoH)
{
    float Fc = Pow5(1.0 - VoH);
    return SpecularColor + (vec3(1.0) - SpecularColor) * Fc;
}

/**
 * Schlick Fresnel with roughness for environment mapping
 */
vec3 F_SchlickRoughness(vec3 SpecularColor, float VoH, float Roughness)
{
    float Fc = Pow5(1.0 - VoH);
    return SpecularColor + (max(vec3(1.0 - Roughness), SpecularColor) - SpecularColor) * Fc;
}

// ============================================================================
// Complete Lighting Models
// ============================================================================

/**
 * Blinn-Phong lighting model
 * Classic lighting model with separate diffuse and specular
 * 
 * @param N Surface normal (normalized)
 * @param V View direction (normalized, pointing towards camera)
 * @param L Light direction (normalized, pointing towards light)
 * @param DiffuseColor Surface diffuse color
 * @param SpecularColor Surface specular color
 * @param Roughness Surface roughness [0, 1]
 * @return Combined diffuse and specular contribution
 */
vec3 BlinnPhong(
    vec3 N,
    vec3 V,
    vec3 L,
    vec3 DiffuseColor,
    vec3 SpecularColor,
    float Roughness)
{
    // Half vector
    vec3 H = normalize(V + L);
    
    // Dot products
    float NoL = max(dot(N, L), 0.0);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);
    
    // Diffuse (Lambert)
    vec3 Diffuse = Diffuse_Lambert(DiffuseColor);
    
    // Specular (Blinn-Phong)
    float D = D_BlinnPhong(Roughness, NoH);
    vec3 F = F_Schlick(SpecularColor, VoH);
    vec3 Specular = D * F;
    
    return (Diffuse + Specular) * NoL;
}

/**
 * Standard PBR lighting (GGX)
 * Physically-based rendering with GGX distribution
 * 
 * @param N Surface normal (normalized)
 * @param V View direction (normalized, pointing towards camera)
 * @param L Light direction (normalized, pointing towards light)
 * @param DiffuseColor Surface diffuse color
 * @param SpecularColor Surface specular color (F0)
 * @param Roughness Surface roughness [0, 1]
 * @return Combined diffuse and specular contribution
 */
vec3 StandardShading(
    vec3 N,
    vec3 V,
    vec3 L,
    vec3 DiffuseColor,
    vec3 SpecularColor,
    float Roughness)
{
    // Half vector
    vec3 H = normalize(V + L);
    
    // Dot products
    float NoL = max(dot(N, L), 0.0);
    float NoV = max(dot(N, V), 0.0001);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);
    
    // Roughness remapping
    float a = Roughness * Roughness;
    float a2 = a * a;
    
    // Diffuse (Lambert)
    vec3 Diffuse = Diffuse_Lambert(DiffuseColor);
    
    // Specular (GGX)
    float D = D_GGX(a2, NoH);
    float Vis = Vis_SmithJointApprox(a2, NoV, NoL);
    vec3 F = F_Schlick(SpecularColor, VoH);
    vec3 Specular = D * Vis * F;
    
    return (Diffuse + Specular) * NoL;
}

/**
 * Simple shading for particles and simple materials
 * 
 * @param DiffuseColor Surface diffuse color
 * @param SpecularColor Surface specular color
 * @param Roughness Surface roughness
 * @param L Light direction
 * @param V View direction
 * @param N Surface normal
 * @return Shading result
 */
vec3 SimpleShading(
    vec3 DiffuseColor,
    vec3 SpecularColor,
    float Roughness,
    vec3 L,
    vec3 V,
    vec3 N)
{
    float NoL = saturate(dot(N, L));
    
    // Simple diffuse
    vec3 Diffuse = DiffuseColor * NoL;
    
    // Simple specular (Blinn-Phong)
    vec3 H = normalize(V + L);
    float NoH = saturate(dot(N, H));
    float SpecPower = max(2.0 / (Roughness * Roughness + 0.0001) - 2.0, 1.0);
    vec3 Specular = SpecularColor * pow(NoH, SpecPower) * NoL;
    
    return Diffuse + Specular;
}

// ============================================================================
// Complete Light Evaluation
// ============================================================================

/**
 * Evaluate a single light contribution
 * 
 * @param WorldPos World position of the surface point
 * @param N Surface normal (normalized)
 * @param V View direction (normalized)
 * @param DiffuseColor Surface diffuse color
 * @param SpecularColor Surface specular color
 * @param Roughness Surface roughness
 * @param LightData Light parameters
 * @param SpecularScale Scale factor for specular
 * @return Light contribution (diffuse + specular)
 */
vec3 EvaluateLight(
    vec3 WorldPos,
    vec3 N,
    vec3 V,
    vec3 DiffuseColor,
    vec3 SpecularColor,
    float Roughness,
    LightShaderParameters LightData,
    float SpecularScale)
{
    vec3 ToLight;
    vec3 L;
    float Attenuation = GetLocalLightAttenuation(WorldPos, LightData, ToLight, L);
    
    if (Attenuation <= 0.0)
    {
        return vec3(0.0);
    }
    
    // Calculate shading
    vec3 Shading = StandardShading(N, V, L, DiffuseColor, SpecularColor * SpecularScale, Roughness);
    
    return LightData.Color * Attenuation * Shading;
}

/**
 * Evaluate directional light contribution
 * 
 * @param N Surface normal (normalized)
 * @param V View direction (normalized)
 * @param DiffuseColor Surface diffuse color
 * @param SpecularColor Surface specular color
 * @param Roughness Surface roughness
 * @param LightData Directional light parameters
 * @return Light contribution
 */
vec3 EvaluateDirectionalLight(
    vec3 N,
    vec3 V,
    vec3 DiffuseColor,
    vec3 SpecularColor,
    float Roughness,
    DirectionalLightData LightData)
{
    if (LightData.HasDirectionalLight == 0u)
    {
        return vec3(0.0);
    }
    
    vec3 L = -LightData.Direction; // Direction TO the light
    
    // Calculate shading
    vec3 Shading = StandardShading(N, V, L, DiffuseColor, SpecularColor, Roughness);
    
    return LightData.Color * Shading;
}

// ============================================================================
// Ambient Occlusion
// ============================================================================

/**
 * Simple ambient occlusion approximation
 * 
 * @param NoV Dot product of normal and view direction
 * @param AO Ambient occlusion factor [0, 1]
 * @return Modified AO for specular
 */
float ComputeSpecularOcclusion(float NoV, float AO)
{
    // Approximation from "Practical Realtime Strategies for Accurate Indirect Occlusion"
    return saturate(pow(NoV + AO, exp2(-16.0 * (1.0 - AO) - 1.0)) - 1.0 + AO);
}

/**
 * Apply ambient lighting with occlusion
 * 
 * @param DiffuseColor Surface diffuse color
 * @param AmbientColor Ambient light color
 * @param AO Ambient occlusion factor [0, 1]
 * @return Ambient contribution
 */
vec3 ApplyAmbientLighting(vec3 DiffuseColor, vec3 AmbientColor, float AO)
{
    return DiffuseColor * AmbientColor * AO;
}

#endif // LIGHTING_COMMON_GLSL
