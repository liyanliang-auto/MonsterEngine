#version 450
// Copyright Monster Engine. All Rights Reserved.

/**
 * @file BasicLit.frag
 * @brief Basic lit fragment shader with Blinn-Phong and PBR lighting
 * 
 * Supports:
 * - Directional light
 * - Multiple point/spot lights
 * - Blinn-Phong and GGX BRDF
 * - Lambert diffuse
 * - Ambient occlusion
 * 
 * Reference UE5:
 * - Engine/Shaders/Private/DeferredLightingCommon.ush
 * - Engine/Shaders/Private/ShadingModels.ush
 */

// ============================================================================
// Constants
// ============================================================================

#define PI 3.14159265359
#define INV_PI 0.31830988618

#define LIGHT_TYPE_DIRECTIONAL 0
#define LIGHT_TYPE_POINT       1
#define LIGHT_TYPE_SPOT        2

#define MAX_LOCAL_LIGHTS 16

// ============================================================================
// Fragment Input
// ============================================================================

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragTangent;
layout(location = 4) in vec3 fragViewDir;

// ============================================================================
// Fragment Output
// ============================================================================

layout(location = 0) out vec4 outColor;

// ============================================================================
// Uniform Buffers
// ============================================================================

// View data
layout(set = 0, binding = 0) uniform ViewData
{
    mat4 ViewMatrix;
    mat4 ProjectionMatrix;
    mat4 ViewProjectionMatrix;
    vec3 CameraPosition;
    float Padding0;
} View;

// Directional light
layout(set = 0, binding = 1) uniform DirectionalLightData
{
    vec3  Direction;        // Direction light is pointing (normalized)
    float Padding0;
    vec3  Color;            // Color * Intensity
    float SourceRadius;     // For soft shadows
} DirectionalLight;

// Local lights (point/spot)
struct LocalLight
{
    vec4 PositionAndInvRadius;      // xyz = position, w = 1/radius
    vec4 ColorAndFalloffExponent;   // xyz = color, w = falloff (0 = inverse squared)
    vec4 DirectionAndType;          // xyz = direction, w = light type
    vec4 SpotAnglesAndSpecular;     // xy = spot angles, z = specular scale, w = unused
};

layout(set = 0, binding = 2) uniform LightBuffer
{
    uint NumLocalLights;
    uint Padding1;
    uint Padding2;
    uint Padding3;
    LocalLight Lights[MAX_LOCAL_LIGHTS];
} LightData;

// Material properties
layout(set = 2, binding = 0) uniform MaterialData
{
    vec4  BaseColor;        // RGB = color, A = opacity
    float Metallic;
    float Roughness;
    float AmbientOcclusion;
    float Padding0;
    vec3  EmissiveColor;
    float Padding1;
} Material;

// Textures
layout(set = 2, binding = 1) uniform sampler2D BaseColorTexture;
layout(set = 2, binding = 2) uniform sampler2D NormalTexture;
layout(set = 2, binding = 3) uniform sampler2D MetallicRoughnessTexture;
layout(set = 2, binding = 4) uniform sampler2D AOTexture;

// ============================================================================
// Utility Functions
// ============================================================================

float Square(float x)
{
    return x * x;
}

float Pow4(float x)
{
    float x2 = x * x;
    return x2 * x2;
}

float Pow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}

// ============================================================================
// Attenuation Functions
// ============================================================================

/**
 * Distance attenuation with smooth falloff
 * Reference UE5: GetDistanceAttenuation in DeferredLightingCommon.ush
 */
float GetDistanceAttenuation(float DistanceSqr, float InvRadiusSqr)
{
    // Physically-based inverse square falloff with smooth cutoff
    float DistanceAttenuation = 1.0 / (DistanceSqr + 1.0);
    
    // Smooth radius falloff: saturate(1 - (d/r)^4)^2
    float Factor = DistanceSqr * InvRadiusSqr;
    float SmoothFactor = clamp(1.0 - Factor * Factor, 0.0, 1.0);
    SmoothFactor = SmoothFactor * SmoothFactor;
    
    return DistanceAttenuation * SmoothFactor;
}

/**
 * Spot light cone attenuation
 */
float SpotAttenuation(vec3 L, vec3 LightDirection, vec2 SpotAngles)
{
    float CosAngle = dot(-L, LightDirection);
    float Attenuation = clamp((CosAngle - SpotAngles.x) * SpotAngles.y, 0.0, 1.0);
    return Attenuation * Attenuation;
}

// ============================================================================
// BRDF Functions
// ============================================================================

/**
 * Lambert diffuse BRDF
 */
vec3 Diffuse_Lambert(vec3 DiffuseColor)
{
    return DiffuseColor * INV_PI;
}

/**
 * GGX Normal Distribution Function
 * Reference UE5: D_GGX in BRDF.ush
 */
float D_GGX(float a2, float NoH)
{
    float d = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * d * d);
}

/**
 * Blinn-Phong Distribution (for comparison/fallback)
 */
float D_BlinnPhong(float Roughness, float NoH)
{
    float a = max(Roughness * Roughness, 0.001);
    float a2 = a * a;
    float n = 2.0 / a2 - 2.0;
    float normalization = (n + 2.0) * INV_PI * 0.5;
    return normalization * pow(max(NoH, 0.0), n);
}

/**
 * Smith-GGX Visibility Function (approximation)
 * Reference UE5: Vis_SmithJointApprox in BRDF.ush
 */
float Vis_SmithJointApprox(float a2, float NoV, float NoL)
{
    float a = sqrt(a2);
    float Vis_SmithV = NoL * (NoV * (1.0 - a) + a);
    float Vis_SmithL = NoV * (NoL * (1.0 - a) + a);
    return 0.5 / (Vis_SmithV + Vis_SmithL + 0.0001);
}

/**
 * Schlick Fresnel Approximation
 * Reference UE5: F_Schlick in BRDF.ush
 */
vec3 F_Schlick(vec3 F0, float VoH)
{
    float Fc = Pow5(1.0 - VoH);
    return F0 + (vec3(1.0) - F0) * Fc;
}

// ============================================================================
// Lighting Calculation
// ============================================================================

/**
 * Standard PBR shading (GGX)
 */
vec3 StandardShading(
    vec3 N,
    vec3 V,
    vec3 L,
    vec3 DiffuseColor,
    vec3 SpecularColor,
    float Roughness)
{
    vec3 H = normalize(V + L);
    
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
 * Blinn-Phong shading (simpler, faster)
 */
vec3 BlinnPhongShading(
    vec3 N,
    vec3 V,
    vec3 L,
    vec3 DiffuseColor,
    vec3 SpecularColor,
    float Roughness)
{
    vec3 H = normalize(V + L);
    
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
 * Evaluate directional light
 */
vec3 EvaluateDirectionalLight(
    vec3 N,
    vec3 V,
    vec3 DiffuseColor,
    vec3 SpecularColor,
    float Roughness)
{
    vec3 L = -DirectionalLight.Direction;
    vec3 Shading = StandardShading(N, V, L, DiffuseColor, SpecularColor, Roughness);
    return DirectionalLight.Color * Shading;
}

/**
 * Evaluate a local light (point or spot)
 */
vec3 EvaluateLocalLight(
    vec3 WorldPos,
    vec3 N,
    vec3 V,
    vec3 DiffuseColor,
    vec3 SpecularColor,
    float Roughness,
    LocalLight Light)
{
    vec3 ToLight = Light.PositionAndInvRadius.xyz - WorldPos;
    float DistanceSqr = dot(ToLight, ToLight);
    vec3 L = ToLight * inversesqrt(DistanceSqr);
    
    // Distance attenuation
    float InvRadius = Light.PositionAndInvRadius.w;
    float InvRadiusSqr = InvRadius * InvRadius;
    float Attenuation = GetDistanceAttenuation(DistanceSqr, InvRadiusSqr);
    
    // Spot light cone attenuation
    uint LightType = uint(Light.DirectionAndType.w);
    if (LightType == LIGHT_TYPE_SPOT)
    {
        vec2 SpotAngles = Light.SpotAnglesAndSpecular.xy;
        Attenuation *= SpotAttenuation(L, Light.DirectionAndType.xyz, SpotAngles);
    }
    
    if (Attenuation <= 0.0)
    {
        return vec3(0.0);
    }
    
    // Calculate shading
    float SpecularScale = Light.SpotAnglesAndSpecular.z;
    vec3 Shading = StandardShading(N, V, L, DiffuseColor, SpecularColor * SpecularScale, Roughness);
    
    return Light.ColorAndFalloffExponent.xyz * Attenuation * Shading;
}

// ============================================================================
// Ambient Occlusion
// ============================================================================

/**
 * Specular occlusion approximation
 * Reference: "Practical Realtime Strategies for Accurate Indirect Occlusion"
 */
float ComputeSpecularOcclusion(float NoV, float AO)
{
    return clamp(pow(NoV + AO, exp2(-16.0 * (1.0 - AO) - 1.0)) - 1.0 + AO, 0.0, 1.0);
}

// ============================================================================
// Main
// ============================================================================

void main()
{
    // Sample textures
    vec4 BaseColorSample = texture(BaseColorTexture, fragTexCoord);
    vec4 MetallicRoughnessSample = texture(MetallicRoughnessTexture, fragTexCoord);
    float AOSample = texture(AOTexture, fragTexCoord).r;
    
    // Material properties
    vec3 BaseColor = Material.BaseColor.rgb * BaseColorSample.rgb;
    float Metallic = Material.Metallic * MetallicRoughnessSample.b;
    float Roughness = Material.Roughness * MetallicRoughnessSample.g;
    float AO = Material.AmbientOcclusion * AOSample;
    
    // Clamp roughness to avoid divide by zero
    Roughness = clamp(Roughness, 0.04, 1.0);
    
    // Derive diffuse and specular colors from base color and metallic
    // Non-metals: diffuse = base color, specular = 0.04
    // Metals: diffuse = 0, specular = base color
    vec3 DiffuseColor = BaseColor * (1.0 - Metallic);
    vec3 SpecularColor = mix(vec3(0.04), BaseColor, Metallic);
    
    // Get normalized vectors
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(fragViewDir);
    
    // Accumulate lighting
    vec3 TotalLighting = vec3(0.0);
    
    // Directional light
    TotalLighting += EvaluateDirectionalLight(N, V, DiffuseColor, SpecularColor, Roughness);
    
    // Local lights
    for (uint i = 0u; i < LightData.NumLocalLights && i < MAX_LOCAL_LIGHTS; ++i)
    {
        TotalLighting += EvaluateLocalLight(
            fragWorldPos, N, V,
            DiffuseColor, SpecularColor, Roughness,
            LightData.Lights[i]
        );
    }
    
    // Apply ambient occlusion
    TotalLighting *= AO;
    
    // Simple ambient lighting
    vec3 AmbientColor = vec3(0.03);
    vec3 Ambient = DiffuseColor * AmbientColor * AO;
    
    // Add emissive
    vec3 Emissive = Material.EmissiveColor;
    
    // Final color
    vec3 FinalColor = TotalLighting + Ambient + Emissive;
    
    // Output
    outColor = vec4(FinalColor, Material.BaseColor.a * BaseColorSample.a);
}
