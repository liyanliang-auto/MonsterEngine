// Copyright Monster Engine. All Rights Reserved.
// 
// StandardPBR_GL.frag - Standard PBR Fragment Shader (OpenGL)
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
// OpenGL version differences from Vulkan:
// - Uses #version 330 core for wider compatibility
// - Uses uniform blocks without explicit set/binding
// - Samplers use uniform declarations instead of layout bindings
//
// Reference: UE5 BasePassPixelShader.usf, DeferredLightingCommon.ush

#version 330 core

// ============================================================================
// Constants
// ============================================================================

#define PI 3.14159265359
#define INV_PI 0.31830988618
#define MIN_ROUGHNESS 0.04
#define MIN_N_DOT_V 1e-4

#define MAX_DIRECTIONAL_LIGHTS 4
#define MAX_LOCAL_LIGHTS 16

// Light type constants
#define LIGHT_TYPE_DIRECTIONAL 0
#define LIGHT_TYPE_POINT       1
#define LIGHT_TYPE_SPOT        2

// Shading model IDs
#define SHADING_MODEL_UNLIT              0
#define SHADING_MODEL_DEFAULT_LIT        1
#define SHADING_MODEL_SUBSURFACE         2
#define SHADING_MODEL_CLEAR_COAT         4
#define SHADING_MODEL_TWO_SIDED_FOLIAGE  6
#define SHADING_MODEL_CLOTH              8

// ============================================================================
// Fragment Input (from Vertex Shader)
// ============================================================================

in vec3 vWorldPosition;
in vec3 vWorldNormal;
in vec2 vTexCoord;
in vec3 vWorldTangent;
in vec3 vWorldBitangent;
in vec3 vViewDirection;
in vec4 vClipPosition;

// ============================================================================
// Fragment Output
// ============================================================================

out vec4 outColor;

// ============================================================================
// Utility Functions
// ============================================================================

float Pow2(float x) { return x * x; }
float Pow4(float x) { float x2 = x * x; return x2 * x2; }
float Pow5(float x) { float x2 = x * x; return x2 * x2 * x; }
float Square(float x) { return x * x; }
float ClampRoughness(float r) { return max(r, MIN_ROUGHNESS); }

// ============================================================================
// Uniform Buffers
// ============================================================================

// View uniform buffer
layout(std140) uniform ViewData
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
    vec4 Direction;
    vec4 Color;
    vec4 ShadowParams;
    mat4 LightViewProj;
};

// Local light data
struct LocalLightData
{
    vec4 PositionAndInvRadius;
    vec4 ColorAndFalloff;
    vec4 DirectionAndAngle;
    vec4 SpotParams;
};

// Light uniform buffer
layout(std140) uniform LightData
{
    DirectionalLightData DirectionalLights[MAX_DIRECTIONAL_LIGHTS];
    LocalLightData LocalLights[MAX_LOCAL_LIGHTS];
    vec4 AmbientColor;
    vec4 EnvironmentParams;
    int NumDirectionalLights;
    int NumLocalLights;
    float ShadowDistance;
    float LightPadding;
} Lights;

// Material uniform buffer
layout(std140) uniform MaterialData
{
    vec4 BaseColor;
    vec4 EmissiveColor;
    float Metallic;
    float Roughness;
    float AmbientOcclusion;
    float NormalScale;
    float AlphaCutoff;
    float Reflectance;
    float ClearCoat;
    float ClearCoatRoughness;
    float Anisotropy;
    float SubsurfaceOpacity;
    float Cloth;
    int   ShadingModel;
    vec4 SubsurfaceColor;
} Material;

// ============================================================================
// Texture Samplers
// ============================================================================

uniform sampler2D BaseColorMap;
uniform sampler2D NormalMap;
uniform sampler2D MetallicRoughnessMap;
uniform sampler2D OcclusionMap;
uniform sampler2D EmissiveMap;
uniform sampler2D ShadowMap;
uniform samplerCube EnvironmentMap;
uniform samplerCube IrradianceMap;
uniform sampler2D BRDFLutMap;

// ============================================================================
// BRDF Functions (Inlined for OpenGL 3.3 compatibility)
// ============================================================================

// Lambert diffuse
vec3 Diffuse_Lambert(vec3 diffuseColor)
{
    return diffuseColor * INV_PI;
}

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

vec3 EnvBRDFApprox(vec3 specularColor, float roughness, float NoV)
{
    vec2 AB = EnvBRDFApproxLazarov(roughness, NoV);
    float F90 = clamp(50.0 * specularColor.g, 0.0, 1.0);
    return specularColor * AB.x + F90 * AB.y;
}

// ============================================================================
// Shadow Functions
// ============================================================================

float SampleShadowPCF(vec4 shadowCoord, float bias)
{
    vec3 projCoords = shadowCoord.xyz / shadowCoord.w;
    projCoords = projCoords * 0.5 + 0.5;
    
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z > 1.0)
    {
        return 1.0;
    }
    
    vec2 texelSize = 1.0 / textureSize(ShadowMap, 0);
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
    vec3 normalMapSample = texture(NormalMap, texCoord).xyz;
    vec3 tangentNormal = normalMapSample * 2.0 - 1.0;
    tangentNormal.xy *= Material.NormalScale;
    tangentNormal = normalize(tangentNormal);
    
    mat3 TBN = mat3(normalize(tangent), normalize(bitangent), normalize(normal));
    return normalize(TBN * tangentNormal);
}

// ============================================================================
// Light Attenuation
// ============================================================================

float CalculateAttenuation(vec3 worldPos, vec3 lightPos, float invRadius, float falloffExponent)
{
    vec3 toLight = lightPos - worldPos;
    float dist = length(toLight);
    float radiusMask = 1.0 - clamp(dist * invRadius, 0.0, 1.0);
    radiusMask *= radiusMask;
    return pow(radiusMask, falloffExponent);
}

float CalculateSpotAttenuation(vec3 lightDir, vec3 spotDir, float cosOuter, float invAngleRange)
{
    float cosAngle = dot(-lightDir, spotDir);
    float spotAttenuation = clamp((cosAngle - cosOuter) * invAngleRange, 0.0, 1.0);
    return spotAttenuation * spotAttenuation;
}

// ============================================================================
// Shading Models
// ============================================================================

struct DirectLighting
{
    vec3 Diffuse;
    vec3 Specular;
    vec3 Transmission;
};

DirectLighting ShadingModel_DefaultLit(
    vec3 diffuseColor, vec3 specularColor, float roughness,
    vec3 N, vec3 V, vec3 L, vec3 lightColor, float attenuation, float NoL)
{
    DirectLighting lighting;
    lighting.Diffuse = vec3(0.0);
    lighting.Specular = vec3(0.0);
    lighting.Transmission = vec3(0.0);
    
    if (NoL <= 0.0) return lighting;
    
    vec3 H = normalize(V + L);
    float NoV = max(dot(N, V), MIN_N_DOT_V);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);
    
    float a2 = Pow4(roughness);
    
    // Diffuse
    lighting.Diffuse = Diffuse_Lambert(diffuseColor);
    lighting.Diffuse *= lightColor * (attenuation * NoL);
    
    // Specular
    float D = D_GGX(a2, NoH);
    float Vis = Vis_SmithJointApprox(a2, NoV, NoL);
    vec3 F = F_Schlick(specularColor, VoH);
    
    lighting.Specular = (D * Vis) * F;
    lighting.Specular *= lightColor * (attenuation * NoL);
    
    return lighting;
}

DirectLighting ShadingModel_ClearCoat(
    vec3 diffuseColor, vec3 specularColor, float roughness,
    float clearCoat, float clearCoatRoughness,
    vec3 N, vec3 V, vec3 L, vec3 lightColor, float attenuation, float NoL)
{
    DirectLighting lighting;
    lighting.Diffuse = vec3(0.0);
    lighting.Specular = vec3(0.0);
    lighting.Transmission = vec3(0.0);
    
    if (NoL <= 0.0) return lighting;
    
    vec3 H = normalize(V + L);
    float NoV = max(dot(N, V), MIN_N_DOT_V);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);
    
    // Clear coat layer
    float F0_coat = 0.04;
    float Fc = Pow5(1.0 - VoH);
    float F_coat = Fc + (1.0 - Fc) * F0_coat;
    
    float alpha_coat = clearCoatRoughness * clearCoatRoughness;
    float a2_coat = alpha_coat * alpha_coat;
    
    float D_coat = D_GGX(a2_coat, NoH);
    float Vis_coat = Vis_SmithJointApprox(a2_coat, NoV, NoL);
    
    vec3 specular_coat = vec3(clearCoat * D_coat * Vis_coat * F_coat);
    specular_coat *= lightColor * (attenuation * NoL);
    
    float fresnelCoeff = (1.0 - F_coat) * (1.0 - F_coat);
    
    // Base layer
    float a2 = Pow4(roughness);
    
    vec3 diffuse_base = Diffuse_Lambert(diffuseColor);
    lighting.Diffuse = mix(diffuse_base, fresnelCoeff * diffuse_base, clearCoat);
    lighting.Diffuse *= lightColor * (attenuation * NoL);
    
    float D_base = D_GGX(a2, NoH);
    float Vis_base = Vis_SmithJointApprox(a2, NoV, NoL);
    vec3 F_base = F_Schlick(specularColor, VoH);
    
    vec3 specular_base = (D_base * Vis_base) * F_base;
    vec3 specular_refracted = fresnelCoeff * specular_base;
    
    lighting.Specular = specular_coat + mix(specular_base, specular_refracted, clearCoat) * lightColor * (attenuation * NoL);
    
    return lighting;
}

DirectLighting ShadingModel_Cloth(
    vec3 diffuseColor, vec3 specularColor, vec3 fuzzColor,
    float roughness, float cloth,
    vec3 N, vec3 V, vec3 L, vec3 lightColor, float attenuation, float NoL)
{
    DirectLighting lighting;
    lighting.Diffuse = vec3(0.0);
    lighting.Specular = vec3(0.0);
    lighting.Transmission = vec3(0.0);
    
    if (NoL <= 0.0) return lighting;
    
    vec3 H = normalize(V + L);
    float NoV = max(dot(N, V), MIN_N_DOT_V);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);
    
    float a2 = Pow4(roughness);
    
    // Diffuse
    lighting.Diffuse = Diffuse_Lambert(diffuseColor);
    lighting.Diffuse *= lightColor * (attenuation * NoL);
    
    // Standard GGX specular
    float D1 = D_GGX(a2, NoH);
    float Vis1 = Vis_SmithJointApprox(a2, NoV, NoL);
    vec3 F1 = F_Schlick(specularColor, VoH);
    vec3 Spec1 = (D1 * Vis1) * F1;
    
    // Cloth specular (inverse GGX)
    float D2 = D_InvGGX(a2, NoH);
    float Vis2 = Vis_Cloth(NoV, NoL);
    vec3 F2 = F_Schlick(fuzzColor, VoH);
    vec3 Spec2 = (D2 * Vis2) * F2;
    
    lighting.Specular = mix(Spec1, Spec2, cloth);
    lighting.Specular *= lightColor * (attenuation * NoL);
    
    return lighting;
}

DirectLighting ShadingModel_Subsurface(
    vec3 diffuseColor, vec3 specularColor, vec3 subsurfaceColor,
    float roughness, float opacity,
    vec3 N, vec3 V, vec3 L, vec3 lightColor, float attenuation, float NoL)
{
    DirectLighting lighting = ShadingModel_DefaultLit(
        diffuseColor, specularColor, roughness,
        N, V, L, lightColor, attenuation, NoL);
    
    // In-scatter
    float inScatter = pow(clamp(dot(L, -V), 0.0, 1.0), 12.0) * mix(3.0, 0.1, opacity);
    
    // Wrap lighting
    float wrappedDiffuse = pow(clamp(dot(N, L) / 1.5 + 0.5 / 1.5, 0.0, 1.0), 1.5) * (2.5 / 1.5);
    float normalContribution = mix(1.0, wrappedDiffuse, opacity);
    float backScatter = normalContribution / (PI * 2.0);
    
    lighting.Transmission = lightColor * (attenuation * mix(backScatter, 1.0, inScatter)) * subsurfaceColor;
    
    return lighting;
}

DirectLighting ShadingModel_TwoSidedFoliage(
    vec3 diffuseColor, vec3 specularColor, vec3 subsurfaceColor,
    float roughness,
    vec3 N, vec3 V, vec3 L, vec3 lightColor, float attenuation, float NoL)
{
    DirectLighting lighting = ShadingModel_DefaultLit(
        diffuseColor, specularColor, roughness,
        N, V, L, lightColor, attenuation, NoL);
    
    float wrap = 0.5;
    float wrapNoL = clamp((-dot(N, L) + wrap) / Square(1.0 + wrap), 0.0, 1.0);
    
    float VoL = dot(V, L);
    float scatter = D_GGX(0.36, clamp(-VoL, 0.0, 1.0));
    
    lighting.Transmission = lightColor * (attenuation * wrapNoL * scatter) * subsurfaceColor;
    
    return lighting;
}

// ============================================================================
// Main Fragment Shader
// ============================================================================

void main()
{
    // Sample material textures
    vec4 baseColorSample = texture(BaseColorMap, vTexCoord) * Material.BaseColor;
    vec2 metallicRoughness = texture(MetallicRoughnessMap, vTexCoord).rg;
    float aoSample = texture(OcclusionMap, vTexCoord).r;
    vec3 emissiveSample = texture(EmissiveMap, vTexCoord).rgb;
    
    // Alpha test
    if (baseColorSample.a < Material.AlphaCutoff)
    {
        discard;
    }
    
    // Material properties
    vec3 baseColor = baseColorSample.rgb;
    float metallic = metallicRoughness.r * Material.Metallic;
    float roughness = ClampRoughness(metallicRoughness.g * Material.Roughness);
    float ao = aoSample * Material.AmbientOcclusion;
    vec3 emissive = emissiveSample * Material.EmissiveColor.rgb * Material.EmissiveColor.w;
    
    // Derived properties
    vec3 dielectricF0 = vec3(0.04);
    vec3 F0 = mix(dielectricF0, baseColor, metallic);
    vec3 diffuseColor = baseColor * (1.0 - metallic);
    vec3 specularColor = F0;
    
    // Vectors
    vec3 N = GetWorldNormal(vWorldNormal, vWorldTangent, vWorldBitangent, vTexCoord);
    vec3 V = normalize(vViewDirection);
    float NoV = max(dot(N, V), MIN_N_DOT_V);
    
    // Accumulate lighting
    vec3 directDiffuse = vec3(0.0);
    vec3 directSpecular = vec3(0.0);
    vec3 transmission = vec3(0.0);
    
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
            vec4 shadowCoord = light.LightViewProj * vec4(vWorldPosition, 1.0);
            shadow = SampleShadowPCF(shadowCoord, light.ShadowParams.x);
            shadow = mix(1.0, shadow, light.ShadowParams.y);
        }
        
        DirectLighting lighting;
        
        if (Material.ShadingModel == SHADING_MODEL_CLEAR_COAT)
        {
            lighting = ShadingModel_ClearCoat(
                diffuseColor, specularColor, roughness,
                Material.ClearCoat, max(Material.ClearCoatRoughness, 0.02),
                N, V, L, lightColor, shadow, NoL);
        }
        else if (Material.ShadingModel == SHADING_MODEL_CLOTH)
        {
            lighting = ShadingModel_Cloth(
                diffuseColor, specularColor, Material.SubsurfaceColor.rgb,
                roughness, Material.Cloth,
                N, V, L, lightColor, shadow, NoL);
        }
        else if (Material.ShadingModel == SHADING_MODEL_SUBSURFACE)
        {
            lighting = ShadingModel_Subsurface(
                diffuseColor, specularColor, Material.SubsurfaceColor.rgb,
                roughness, Material.SubsurfaceOpacity,
                N, V, L, lightColor, shadow, NoL);
        }
        else if (Material.ShadingModel == SHADING_MODEL_TWO_SIDED_FOLIAGE)
        {
            lighting = ShadingModel_TwoSidedFoliage(
                diffuseColor, specularColor, Material.SubsurfaceColor.rgb,
                roughness, N, V, L, lightColor, shadow, NoL);
        }
        else
        {
            lighting = ShadingModel_DefaultLit(
                diffuseColor, specularColor, roughness,
                N, V, L, lightColor, shadow, NoL);
        }
        
        directDiffuse += lighting.Diffuse;
        directSpecular += lighting.Specular;
        transmission += lighting.Transmission;
    }
    
    // Local Lights
    for (int i = 0; i < Lights.NumLocalLights && i < MAX_LOCAL_LIGHTS; ++i)
    {
        LocalLightData light = Lights.LocalLights[i];
        
        vec3 lightPos = light.PositionAndInvRadius.xyz;
        float invRadius = light.PositionAndInvRadius.w;
        vec3 lightColor = light.ColorAndFalloff.rgb;
        float falloff = light.ColorAndFalloff.w;
        int lightType = int(light.SpotParams.z);
        
        vec3 toLight = lightPos - vWorldPosition;
        float dist = length(toLight);
        vec3 L = toLight / dist;
        
        float attenuation = CalculateAttenuation(vWorldPosition, lightPos, invRadius, falloff);
        
        if (lightType == LIGHT_TYPE_SPOT)
        {
            vec3 spotDir = normalize(light.DirectionAndAngle.xyz);
            float cosOuter = light.DirectionAndAngle.w;
            float invAngleRange = light.SpotParams.y;
            attenuation *= CalculateSpotAttenuation(L, spotDir, cosOuter, invAngleRange);
        }
        
        if (attenuation <= 0.0) continue;
        
        float NoL = max(dot(N, L), 0.0);
        
        DirectLighting lighting = ShadingModel_DefaultLit(
            diffuseColor, specularColor, roughness,
            N, V, L, lightColor, attenuation, NoL);
        
        directDiffuse += lighting.Diffuse;
        directSpecular += lighting.Specular;
    }
    
    // IBL
    vec3 irradiance = texture(IrradianceMap, N).rgb * Lights.EnvironmentParams.x;
    vec3 R = reflect(-V, N);
    float mipLevel = roughness * 4.0;
    vec3 prefilteredColor = textureLod(EnvironmentMap, R, mipLevel).rgb * Lights.EnvironmentParams.x;
    
    vec3 kS = F_Schlick(F0, NoV);
    vec3 kD = (1.0 - kS) * (1.0 - metallic);
    vec3 indirectDiffuse = kD * irradiance * diffuseColor;
    
    vec2 brdfLut = texture(BRDFLutMap, vec2(NoV, roughness)).rg;
    vec3 indirectSpecular = prefilteredColor * (specularColor * brdfLut.x + brdfLut.y);
    
    indirectDiffuse *= ao;
    indirectSpecular *= ao;
    
    // Ambient
    vec3 ambient = Lights.AmbientColor.rgb * Lights.AmbientColor.w * diffuseColor * ao;
    
    // Final composition
    vec3 color = directDiffuse + directSpecular + transmission +
                 indirectDiffuse + indirectSpecular + ambient + emissive;
    
    // Tone mapping (Reinhard)
    color = color / (color + vec3(1.0));
    
    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));
    
    outColor = vec4(color, baseColorSample.a);
}
