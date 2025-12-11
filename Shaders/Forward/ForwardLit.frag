// Copyright Monster Engine. All Rights Reserved.
// Forward Lighting Fragment Shader
// 
// This shader implements forward lighting with support for:
// - Multiple light types (directional, point, spot)
// - PBR materials (metallic-roughness workflow)
// - Normal mapping
// - Shadow mapping with PCF filtering
// - Ambient occlusion
//
// Reference: UE5 BasePassPixelShader.usf, DeferredLightingCommon.ush

#version 450

// ============================================================================
// Constants
// ============================================================================

#define MAX_DIRECTIONAL_LIGHTS 4
#define MAX_LOCAL_LIGHTS 8
#define PI 3.14159265359
#define INV_PI 0.31830988618

// Light type constants
#define LIGHT_TYPE_DIRECTIONAL 0
#define LIGHT_TYPE_POINT 1
#define LIGHT_TYPE_SPOT 2
#define LIGHT_TYPE_RECT 3

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

// View uniform buffer (same as vertex shader)
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

// Light uniform buffer (binding 2, set 0)
layout(set = 0, binding = 2) uniform LightData
{
    DirectionalLightData DirectionalLights[MAX_DIRECTIONAL_LIGHTS];
    LocalLightData LocalLights[MAX_LOCAL_LIGHTS];
    vec4 AmbientColor;          // xyz = ambient color, w = ambient intensity
    int NumDirectionalLights;
    int NumLocalLights;
    float ShadowDistance;
    float Padding;
} Lights;

// Material uniform buffer (binding 3, set 0)
layout(set = 0, binding = 3) uniform MaterialData
{
    vec4 BaseColor;             // xyz = base color, w = alpha
    vec4 EmissiveColor;         // xyz = emissive color, w = emissive intensity
    float Metallic;             // 0 = dielectric, 1 = metal
    float Roughness;            // 0 = smooth, 1 = rough
    float AmbientOcclusion;     // Pre-baked AO
    float NormalScale;          // Normal map intensity
    float AlphaCutoff;          // Alpha test threshold
    float Reflectance;          // Dielectric reflectance (default 0.5 = 4% F0)
    float ClearCoat;            // Clear coat intensity
    float ClearCoatRoughness;   // Clear coat roughness
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

// ============================================================================
// BRDF Functions
// ============================================================================

// Fresnel-Schlick approximation
vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Fresnel-Schlick with roughness for IBL
vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// GGX/Trowbridge-Reitz normal distribution function
float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return num / max(denom, 0.0001);
}

// Smith's geometry function (Schlick-GGX)
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return num / max(denom, 0.0001);
}

// Smith's geometry function for both view and light directions
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

// ============================================================================
// Lighting Functions
// ============================================================================

// Calculate light attenuation for point/spot lights
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

// Calculate spot light cone attenuation
float CalculateSpotAttenuation(vec3 lightDir, vec3 spotDir, float cosOuter, float invAngleRange)
{
    float cosAngle = dot(-lightDir, spotDir);
    float spotAttenuation = clamp((cosAngle - cosOuter) * invAngleRange, 0.0, 1.0);
    return spotAttenuation * spotAttenuation;
}

// Cook-Torrance BRDF for a single light
vec3 EvaluateBRDF(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float metallic, float roughness, vec3 F0)
{
    vec3 H = normalize(V + L);
    
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    
    // Early out if light is behind surface
    if (NdotL <= 0.0)
    {
        return vec3(0.0);
    }
    
    // Calculate BRDF components
    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = FresnelSchlick(VdotH, F0);
    
    // Specular BRDF
    vec3 numerator = D * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.0001;
    vec3 specular = numerator / denominator;
    
    // Diffuse BRDF (energy conserving)
    vec3 kS = F;
    vec3 kD = (1.0 - kS) * (1.0 - metallic);
    vec3 diffuse = kD * albedo * INV_PI;
    
    // Combine and apply light
    return (diffuse + specular) * radiance * NdotL;
}

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
// Main Fragment Shader
// ============================================================================

void main()
{
    // Sample material textures
    vec4 baseColorSample = texture(BaseColorMap, inTexCoord) * Material.BaseColor;
    vec2 metallicRoughness = texture(MetallicRoughnessMap, inTexCoord).rg;
    float ao = texture(OcclusionMap, inTexCoord).r * Material.AmbientOcclusion;
    vec3 emissive = texture(EmissiveMap, inTexCoord).rgb * Material.EmissiveColor.rgb * Material.EmissiveColor.w;
    
    // Alpha test
    if (baseColorSample.a < Material.AlphaCutoff)
    {
        discard;
    }
    
    // Material properties
    vec3 albedo = baseColorSample.rgb;
    float metallic = metallicRoughness.r * Material.Metallic;
    float roughness = metallicRoughness.g * Material.Roughness;
    roughness = clamp(roughness, 0.04, 1.0); // Prevent divide by zero in BRDF
    
    // Calculate F0 (base reflectivity)
    // For dielectrics, use reflectance parameter; for metals, use albedo
    vec3 F0 = vec3(0.16 * Material.Reflectance * Material.Reflectance);
    F0 = mix(F0, albedo, metallic);
    
    // Get world-space normal (with normal mapping)
    vec3 N = GetWorldNormal(inWorldNormal, inWorldTangent, inWorldBitangent, inTexCoord);
    vec3 V = normalize(inViewDirection);
    
    // Accumulate lighting
    vec3 Lo = vec3(0.0);
    
    // ========================================
    // Directional Lights
    // ========================================
    for (int i = 0; i < Lights.NumDirectionalLights && i < MAX_DIRECTIONAL_LIGHTS; ++i)
    {
        DirectionalLightData light = Lights.DirectionalLights[i];
        
        vec3 L = -normalize(light.Direction.xyz);
        vec3 radiance = light.Color.rgb;
        
        // Shadow
        float shadow = 1.0;
        if (light.ShadowParams.y > 0.0)
        {
            vec4 shadowCoord = light.LightViewProj * vec4(inWorldPosition, 1.0);
            shadow = SampleShadowPCF(shadowCoord, light.ShadowParams.x);
            shadow = mix(1.0, shadow, light.ShadowParams.y);
        }
        
        // Evaluate BRDF
        Lo += EvaluateBRDF(N, V, L, radiance * shadow, albedo, metallic, roughness, F0);
    }
    
    // ========================================
    // Local Lights (Point/Spot)
    // ========================================
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
        
        vec3 radiance = lightColor * attenuation;
        
        // Evaluate BRDF
        Lo += EvaluateBRDF(N, V, L, radiance, albedo, metallic, roughness, F0);
    }
    
    // ========================================
    // Ambient Lighting
    // ========================================
    vec3 ambient = Lights.AmbientColor.rgb * Lights.AmbientColor.w * albedo * ao;
    
    // Optional: Sample environment map for reflections
    // vec3 R = reflect(-V, N);
    // vec3 envColor = textureLod(EnvironmentMap, R, roughness * 4.0).rgb;
    // vec3 kS = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    // vec3 specularAmbient = envColor * kS;
    // ambient += specularAmbient * ao;
    
    // ========================================
    // Final Color
    // ========================================
    vec3 color = ambient + Lo + emissive;
    
    // Tone mapping (simple Reinhard)
    color = color / (color + vec3(1.0));
    
    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));
    
    outColor = vec4(color, baseColorSample.a);
}
