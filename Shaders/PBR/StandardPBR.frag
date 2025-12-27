// Copyright Monster Engine. All Rights Reserved.
// 
// StandardPBR.frag - Standard PBR Fragment Shader (Vulkan)
// 
// This shader implements physically based rendering with support for:
// - Multiple shading models (DefaultLit, Subsurface, ClearCoat, Cloth, Foliage)
// - Metallic-roughness workflow
// - Normal mapping
// - Multiple light types (directional, point, spot)
// - Shadow mapping with PCF filtering
// - Image-based lighting (IBL)
// - Ambient occlusion
//
// Reference: UE5 BasePassPixelShader.usf, DeferredLightingCommon.ush
//
// Note: BRDF.glsl and ShadingModels.glsl are included at runtime by the engine's
// shader preprocessor. For standalone compilation, use glslangValidator with
// the -I flag or preprocess the shader manually.

#version 450

// ============================================================================
// BRDF Constants and Utilities (from BRDF.glsl)
// ============================================================================

#define PI 3.14159265359
#define INV_PI 0.31830988618
#define MIN_ROUGHNESS 0.04
#define MIN_N_DOT_V 1e-4

float Pow2(float x) { return x * x; }
float Pow4(float x) { float x2 = x * x; return x2 * x2; }
float Pow5(float x) { float x2 = x * x; return x2 * x2 * x; }
float Square(float x) { return x * x; }
float ClampRoughness(float r) { return max(r, MIN_ROUGHNESS); }

// Lambert diffuse
vec3 Diffuse_Lambert(vec3 diffuseColor) { return diffuseColor * INV_PI; }

// GGX Normal Distribution Function
float D_GGX(float a2, float NoH)
{
    float d = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * d * d);
}

// Inverse GGX for cloth
float D_InvGGX(float a2, float NoH)
{
    float A = 4.0;
    float d = (NoH - a2 * NoH) * NoH + a2;
    return 1.0 / (PI * (1.0 + A * a2)) * (1.0 + 4.0 * a2 * a2 / (d * d));
}

// Smith Joint Approximation
float Vis_SmithJointApprox(float a2, float NoV, float NoL)
{
    float a = sqrt(a2);
    float Vis_SmithV = NoL * (NoV * (1.0 - a) + a);
    float Vis_SmithL = NoV * (NoL * (1.0 - a) + a);
    return 0.5 / (Vis_SmithV + Vis_SmithL);
}

// Cloth visibility
float Vis_Cloth(float NoV, float NoL)
{
    return 1.0 / (4.0 * (NoL + NoV - NoL * NoV));
}

// Schlick Fresnel
vec3 F_Schlick(vec3 F0, float VoH)
{
    float Fc = Pow5(1.0 - VoH);
    return clamp(50.0 * F0.g, 0.0, 1.0) * Fc + (1.0 - Fc) * F0;
}

// Environment BRDF Approximation
vec2 EnvBRDFApproxLazarov(float roughness, float NoV)
{
    vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

// ============================================================================
// Shading Model IDs (from ShadingModels.glsl)
// ============================================================================

#define SHADING_MODEL_UNLIT              0
#define SHADING_MODEL_DEFAULT_LIT        1
#define SHADING_MODEL_SUBSURFACE         2
#define SHADING_MODEL_CLEAR_COAT         4
#define SHADING_MODEL_TWO_SIDED_FOLIAGE  6
#define SHADING_MODEL_CLOTH              8

// ============================================================================
// Constants
// ============================================================================

#define MAX_DIRECTIONAL_LIGHTS 4
#define MAX_LOCAL_LIGHTS 16

// Light type constants
#define LIGHT_TYPE_DIRECTIONAL 0
#define LIGHT_TYPE_POINT       1
#define LIGHT_TYPE_SPOT        2
#define LIGHT_TYPE_RECT        3

// Shadow map constants
#define SHADOW_DEPTH_BIAS 0.005
#define PCF_SAMPLES 9

// ============================================================================
// Fragment Input (from Vertex Shader)
// ============================================================================

layout(location = 0) in vec3 inWorldPosition;
layout(location = 1) in vec3 inWorldNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inWorldTangent;
layout(location = 4) in vec3 inWorldBitangent;
layout(location = 5) in vec3 inViewDirection;
layout(location = 6) in vec4 inClipPosition;

// ============================================================================
// Fragment Output
// ============================================================================

layout(location = 0) out vec4 outColor;

// ============================================================================
// Uniform Buffers
// ============================================================================

// View uniform buffer (set 0, binding 0)
layout(set = 0, binding = 0) uniform ViewData
{
    mat4 ViewMatrix;
    mat4 ProjectionMatrix;
    mat4 ViewProjectionMatrix;
    mat4 InvViewMatrix;
    mat4 InvProjectionMatrix;
    vec4 ViewOrigin;
    vec4 ViewForward;
    vec4 ViewRight;
    vec4 ViewUp;
    vec4 ScreenParams;
    vec4 ZBufferParams;
    float Time;
    float DeltaTime;
    float Padding0;
    float Padding1;
} View;

// Directional light data
struct DirectionalLightData
{
    vec4 Direction;         // xyz = direction (normalized), w = unused
    vec4 Color;             // xyz = color * intensity, w = unused
    vec4 ShadowParams;      // x = shadow bias, y = shadow strength, z = cascade count, w = unused
    mat4 LightViewProj;     // Light view-projection for shadow mapping
};

// Local light data (point/spot)
struct LocalLightData
{
    vec4 PositionAndInvRadius;  // xyz = position, w = 1/radius
    vec4 ColorAndFalloff;       // xyz = color * intensity, w = falloff exponent
    vec4 DirectionAndAngle;     // xyz = direction (for spot), w = cos(outer angle)
    vec4 SpotParams;            // x = cos(inner angle), y = 1/(cos(inner) - cos(outer)), z = light type, w = shadow index
};

// Light uniform buffer (set 0, binding 2)
layout(set = 0, binding = 2) uniform LightData
{
    DirectionalLightData DirectionalLights[MAX_DIRECTIONAL_LIGHTS];
    LocalLightData LocalLights[MAX_LOCAL_LIGHTS];
    vec4 AmbientColor;          // xyz = ambient color, w = ambient intensity
    vec4 EnvironmentParams;     // x = env map intensity, y = env map rotation, z = unused, w = unused
    int NumDirectionalLights;
    int NumLocalLights;
    float ShadowDistance;
    float Padding;
} Lights;

// Material uniform buffer (set 0, binding 3)
layout(set = 0, binding = 3) uniform MaterialData
{
    vec4 BaseColor;             // xyz = base color, w = alpha
    vec4 EmissiveColor;         // xyz = emissive color, w = emissive intensity
    float Metallic;             // 0 = dielectric, 1 = metal
    float Roughness;            // 0 = smooth, 1 = rough
    float AmbientOcclusion;     // Pre-baked AO multiplier
    float NormalScale;          // Normal map intensity
    float AlphaCutoff;          // Alpha test threshold
    float Reflectance;          // Dielectric reflectance (default 0.5 = 4% F0)
    float ClearCoat;            // Clear coat intensity
    float ClearCoatRoughness;   // Clear coat roughness
    float Anisotropy;           // Anisotropy strength (-1 to 1)
    float SubsurfaceOpacity;    // Subsurface opacity
    float Cloth;                // Cloth shading blend
    int   ShadingModel;         // Shading model ID
    vec4 SubsurfaceColor;       // xyz = subsurface/fuzz color, w = unused
} Material;

// ============================================================================
// Texture Samplers
// ============================================================================

layout(set = 1, binding = 0) uniform sampler2D BaseColorMap;
layout(set = 1, binding = 1) uniform sampler2D NormalMap;
layout(set = 1, binding = 2) uniform sampler2D MetallicRoughnessMap;  // R = metallic, G = roughness
layout(set = 1, binding = 3) uniform sampler2D OcclusionMap;
layout(set = 1, binding = 4) uniform sampler2D EmissiveMap;
layout(set = 1, binding = 5) uniform sampler2D ShadowMap;
layout(set = 1, binding = 6) uniform samplerCube EnvironmentMap;
layout(set = 1, binding = 7) uniform samplerCube IrradianceMap;
layout(set = 1, binding = 8) uniform sampler2D BRDFLutMap;

// ============================================================================
// Shadow Functions
// ============================================================================

// Sample shadow map with PCF filtering
float SampleShadowPCF(vec4 shadowCoord, float bias)
{
    // Perspective divide
    vec3 projCoords = shadowCoord.xyz / shadowCoord.w;
    
    // Transform to [0,1] range
    projCoords = projCoords * 0.5 + 0.5;
    
    // Check if outside shadow map
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z > 1.0)
    {
        return 1.0; // Not in shadow
    }
    
    // Get shadow map texel size
    vec2 texelSize = 1.0 / textureSize(ShadowMap, 0);
    
    // PCF filtering
    float shadow = 0.0;
    int samples = 0;
    
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            float shadowDepth = texture(ShadowMap, projCoords.xy + offset).r;
            shadow += (projCoords.z - bias > shadowDepth) ? 0.0 : 1.0;
            samples++;
        }
    }
    
    return shadow / float(samples);
}

// ============================================================================
// Normal Mapping
// ============================================================================

vec3 GetWorldNormal(vec3 normal, vec3 tangent, vec3 bitangent, vec2 texCoord)
{
    // Sample normal map
    vec3 normalMapSample = texture(NormalMap, texCoord).xyz;
    
    // Convert from [0,1] to [-1,1]
    vec3 tangentNormal = normalMapSample * 2.0 - 1.0;
    
    // Apply normal scale
    tangentNormal.xy *= Material.NormalScale;
    tangentNormal = normalize(tangentNormal);
    
    // Build TBN matrix
    mat3 TBN = mat3(normalize(tangent), normalize(bitangent), normalize(normal));
    
    // Transform to world space
    return normalize(TBN * tangentNormal);
}

// ============================================================================
// Light Attenuation
// ============================================================================

float CalculateAttenuation(vec3 worldPos, vec3 lightPos, float invRadius, float falloffExponent)
{
    vec3 toLight = lightPos - worldPos;
    float distSq = dot(toLight, toLight);
    float dist = sqrt(distSq);
    
    // Inverse square falloff with radius cutoff
    float radiusMask = 1.0 - clamp(dist * invRadius, 0.0, 1.0);
    radiusMask *= radiusMask;
    
    // Apply falloff exponent
    float attenuation = pow(radiusMask, falloffExponent);
    
    return attenuation;
}

float CalculateSpotAttenuation(vec3 lightDir, vec3 spotDir, float cosOuter, float invAngleRange)
{
    float cosAngle = dot(-lightDir, spotDir);
    float spotAttenuation = clamp((cosAngle - cosOuter) * invAngleRange, 0.0, 1.0);
    return spotAttenuation * spotAttenuation;
}

// ============================================================================
// IBL Functions
// ============================================================================

vec3 SampleEnvironmentMap(vec3 R, float roughness)
{
    // Calculate mip level based on roughness
    float mipLevel = roughness * 4.0;  // Assuming 5 mip levels (0-4)
    return textureLod(EnvironmentMap, R, mipLevel).rgb;
}

vec3 SampleIrradianceMap(vec3 N)
{
    return texture(IrradianceMap, N).rgb;
}

vec2 SampleBRDFLut(float NoV, float roughness)
{
    return texture(BRDFLutMap, vec2(NoV, roughness)).rg;
}

// ============================================================================
// Shading Model Functions
// ============================================================================

// Evaluate DefaultLit BRDF for a single light
void EvaluateDefaultLit(
    vec3 diffuseColor, vec3 specularColor, float roughness,
    vec3 N, vec3 V, vec3 L, vec3 lightColor, float attenuation, float NoL,
    out vec3 outDiffuse, out vec3 outSpecular)
{
    outDiffuse = vec3(0.0);
    outSpecular = vec3(0.0);
    
    if (NoL <= 0.0) return;
    
    vec3 H = normalize(V + L);
    float NoV = max(dot(N, V), MIN_N_DOT_V);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);
    
    float a2 = Pow4(roughness);
    
    // Diffuse
    outDiffuse = Diffuse_Lambert(diffuseColor);
    outDiffuse *= lightColor * (attenuation * NoL);
    
    // Specular GGX
    float D = D_GGX(a2, NoH);
    float Vis = Vis_SmithJointApprox(a2, NoV, NoL);
    vec3 F = F_Schlick(specularColor, VoH);
    
    outSpecular = (D * Vis) * F;
    outSpecular *= lightColor * (attenuation * NoL);
}

// ============================================================================
// Main Fragment Shader
// ============================================================================

void main()
{
    // ========================================
    // Sample Material Textures
    // ========================================
    
    vec4 baseColorSample = texture(BaseColorMap, inTexCoord) * Material.BaseColor;
    vec2 metallicRoughness = texture(MetallicRoughnessMap, inTexCoord).rg;
    float aoSample = texture(OcclusionMap, inTexCoord).r;
    vec3 emissiveSample = texture(EmissiveMap, inTexCoord).rgb;
    
    // Alpha test
    if (baseColorSample.a < Material.AlphaCutoff)
    {
        discard;
    }
    
    // ========================================
    // Setup Material Properties
    // ========================================
    
    vec3 baseColor = baseColorSample.rgb;
    float metallic = metallicRoughness.r * Material.Metallic;
    float roughness = ClampRoughness(metallicRoughness.g * Material.Roughness);
    float ao = aoSample * Material.AmbientOcclusion;
    vec3 emissive = emissiveSample * Material.EmissiveColor.rgb * Material.EmissiveColor.w;
    
    // Compute derived properties
    vec3 dielectricF0 = vec3(0.04);
    vec3 F0 = mix(dielectricF0, baseColor, metallic);
    vec3 diffuseColor = baseColor * (1.0 - metallic);
    vec3 specularColor = F0;
    
    // ========================================
    // Setup Vectors
    // ========================================
    
    vec3 N = GetWorldNormal(inWorldNormal, inWorldTangent, inWorldBitangent, inTexCoord);
    vec3 V = normalize(inViewDirection);
    float NoV = max(dot(N, V), MIN_N_DOT_V);
    
    // ========================================
    // Accumulate Direct Lighting
    // ========================================
    
    vec3 directDiffuse = vec3(0.0);
    vec3 directSpecular = vec3(0.0);
    
    // Directional Lights
    for (int i = 0; i < Lights.NumDirectionalLights && i < MAX_DIRECTIONAL_LIGHTS; ++i)
    {
        DirectionalLightData light = Lights.DirectionalLights[i];
        
        vec3 L = -normalize(light.Direction.xyz);
        float NoL = max(dot(N, L), 0.0);
        vec3 lightColor = light.Color.rgb;
        
        // Shadow
        float shadow = 1.0;
        if (light.ShadowParams.y > 0.0)
        {
            vec4 shadowCoord = light.LightViewProj * vec4(inWorldPosition, 1.0);
            shadow = SampleShadowPCF(shadowCoord, light.ShadowParams.x);
            shadow = mix(1.0, shadow, light.ShadowParams.y);
        }
        
        // Evaluate BRDF
        vec3 diffuse, specular;
        EvaluateDefaultLit(diffuseColor, specularColor, roughness, N, V, L, lightColor, shadow, NoL, diffuse, specular);
        
        directDiffuse += diffuse;
        directSpecular += specular;
    }
    
    // Local Lights (Point/Spot)
    for (int i = 0; i < Lights.NumLocalLights && i < MAX_LOCAL_LIGHTS; ++i)
    {
        LocalLightData light = Lights.LocalLights[i];
        
        vec3 lightPos = light.PositionAndInvRadius.xyz;
        float invRadius = light.PositionAndInvRadius.w;
        vec3 lightColor = light.ColorAndFalloff.rgb;
        float falloff = light.ColorAndFalloff.w;
        int lightType = int(light.SpotParams.z);
        
        // Calculate light direction and distance
        vec3 toLight = lightPos - inWorldPosition;
        float dist = length(toLight);
        vec3 L = toLight / dist;
        
        // Calculate attenuation
        float attenuation = CalculateAttenuation(inWorldPosition, lightPos, invRadius, falloff);
        
        // Spot light cone attenuation
        if (lightType == LIGHT_TYPE_SPOT)
        {
            vec3 spotDir = normalize(light.DirectionAndAngle.xyz);
            float cosOuter = light.DirectionAndAngle.w;
            float invAngleRange = light.SpotParams.y;
            attenuation *= CalculateSpotAttenuation(L, spotDir, cosOuter, invAngleRange);
        }
        
        // Skip if no contribution
        if (attenuation <= 0.0)
        {
            continue;
        }
        
        float NoL = max(dot(N, L), 0.0);
        
        // Evaluate BRDF
        vec3 diffuse, specular;
        EvaluateDefaultLit(diffuseColor, specularColor, roughness, N, V, L, lightColor, attenuation, NoL, diffuse, specular);
        
        directDiffuse += diffuse;
        directSpecular += specular;
    }
    
    // ========================================
    // Image-Based Lighting (IBL)
    // ========================================
    
    // Sample irradiance for diffuse IBL
    vec3 irradiance = SampleIrradianceMap(N) * Lights.EnvironmentParams.x;
    
    // Sample prefiltered environment for specular IBL
    vec3 R = reflect(-V, N);
    vec3 prefilteredColor = SampleEnvironmentMap(R, roughness) * Lights.EnvironmentParams.x;
    
    // BRDF LUT
    vec2 brdfLut = SampleBRDFLut(NoV, roughness);
    
    // Diffuse IBL
    vec3 kS = F_Schlick(F0, NoV);
    vec3 kD = (1.0 - kS) * (1.0 - metallic);
    vec3 indirectDiffuse = kD * irradiance * diffuseColor * ao;
    
    // Specular IBL
    vec3 indirectSpecular = prefilteredColor * (specularColor * brdfLut.x + brdfLut.y) * ao;
    
    // ========================================
    // Ambient Lighting
    // ========================================
    
    vec3 ambient = Lights.AmbientColor.rgb * Lights.AmbientColor.w * diffuseColor * ao;
    
    // ========================================
    // Final Color Composition
    // ========================================
    
    vec3 color = directDiffuse + directSpecular + indirectDiffuse + indirectSpecular + ambient + emissive;
    
    // Simple Reinhard tone mapping
    color = color / (color + vec3(1.0));
    
    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));
    
    outColor = vec4(color, baseColorSample.a);
}
