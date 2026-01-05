//------------------------------------------------------------------------------
// PBR.frag - PBR Fragment Shader (Vulkan GLSL)
//------------------------------------------------------------------------------
// Copyright Monster Engine. All Rights Reserved.
// Reference: Google Filament, UE5 BasePassPixelShader
//
// Implements physically based rendering with:
// - GGX specular BRDF (D_GGX, V_SmithGGXCorrelated, F_Schlick)
// - Lambertian diffuse
// - Direct lighting (point, directional, spot lights)
// - Simple ambient lighting (IBL to be added later)
//
// Row-major matrices, row vector multiplication (UE5 convention)
// Non-reverse Z depth buffer
//------------------------------------------------------------------------------

#version 450

//------------------------------------------------------------------------------
// Include BRDF functions
//------------------------------------------------------------------------------

// Constants
#define PI 3.14159265358979323846
#define INV_PI 0.31830988618379067154
#define EPSILON 1e-6
#define MIN_ROUGHNESS 0.045
#define MIN_N_DOT_V 1e-4

// Maximum number of lights
#define MAX_LIGHTS 8

//------------------------------------------------------------------------------
// Utility Functions
//------------------------------------------------------------------------------

float saturate(float x) {
    return clamp(x, 0.0, 1.0);
}

vec3 saturateVec3(vec3 x) {
    return clamp(x, vec3(0.0), vec3(1.0));
}

float sq(float x) {
    return x * x;
}

float pow5(float x) {
    float x2 = x * x;
    return x2 * x2 * x;
}

//------------------------------------------------------------------------------
// BRDF Functions
//------------------------------------------------------------------------------

// GGX Normal Distribution Function
float D_GGX(float alpha, float NoH) {
    float a2 = alpha * alpha;
    float d = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * d * d + EPSILON);
}

// Smith GGX Correlated Visibility Function
float V_SmithGGXCorrelated(float alpha, float NoV, float NoL) {
    float a2 = alpha * alpha;
    float lambdaV = NoL * sqrt((NoV - a2 * NoV) * NoV + a2);
    float lambdaL = NoV * sqrt((NoL - a2 * NoL) * NoL + a2);
    return 0.5 / (lambdaV + lambdaL + EPSILON);
}

// Schlick Fresnel Approximation
vec3 F_Schlick(vec3 f0, float VoH) {
    return f0 + (1.0 - f0) * pow5(1.0 - VoH);
}

vec3 F_Schlick(vec3 f0, float f90, float VoH) {
    return f0 + (f90 - f0) * pow5(1.0 - VoH);
}

// Lambertian Diffuse
float Fd_Lambert() {
    return INV_PI;
}

//------------------------------------------------------------------------------
// Descriptor Set 0: Per-Frame Data
//------------------------------------------------------------------------------

layout(set = 0, binding = 0) uniform ViewUniformBuffer {
    mat4 ViewMatrix;
    mat4 ProjectionMatrix;
    mat4 ViewProjectionMatrix;
    mat4 InvViewMatrix;
    mat4 InvProjectionMatrix;
    mat4 InvViewProjectionMatrix;
    vec4 CameraPosition;
    vec4 CameraDirection;
    vec4 ViewportSize;
    vec4 TimeParams;
} View;

// Light types
#define LIGHT_TYPE_DIRECTIONAL 0
#define LIGHT_TYPE_POINT       1
#define LIGHT_TYPE_SPOT        2

struct LightData {
    vec4 Position;        // xyz = position (point/spot) or direction (directional), w = type
    vec4 Color;           // xyz = color, w = intensity
    vec4 Direction;       // xyz = direction (spot), w = unused
    vec4 Attenuation;     // x = range, y = inner cone, z = outer cone, w = unused
};

layout(set = 0, binding = 1) uniform LightUniformBuffer {
    LightData Lights[MAX_LIGHTS];
    int NumLights;
    float AmbientIntensity;
    vec2 Padding;
} Lighting;

//------------------------------------------------------------------------------
// Descriptor Set 1: Per-Material Data
//------------------------------------------------------------------------------

layout(set = 1, binding = 0) uniform MaterialUniformBuffer {
    vec4 BaseColorFactor;     // xyz = base color, w = alpha
    vec4 EmissiveFactor;      // xyz = emissive color, w = unused
    float MetallicFactor;
    float RoughnessFactor;
    float ReflectanceFactor;  // Dielectric reflectance (default 0.5 = 4%)
    float AmbientOcclusion;
    float AlphaCutoff;
    float ClearCoat;
    float ClearCoatRoughness;
    float Padding;
} Material;

// Material textures
layout(set = 1, binding = 1) uniform sampler2D BaseColorMap;
layout(set = 1, binding = 2) uniform sampler2D NormalMap;
layout(set = 1, binding = 3) uniform sampler2D MetallicRoughnessMap;  // G = roughness, B = metallic
layout(set = 1, binding = 4) uniform sampler2D OcclusionMap;
layout(set = 1, binding = 5) uniform sampler2D EmissiveMap;

//------------------------------------------------------------------------------
// Fragment Inputs
//------------------------------------------------------------------------------

layout(location = 0) in vec3 inWorldPosition;
layout(location = 1) in vec3 inWorldNormal;
layout(location = 2) in vec4 inWorldTangent;
layout(location = 3) in vec2 inTexCoord0;
layout(location = 4) in vec2 inTexCoord1;
layout(location = 5) in vec4 inVertexColor;
layout(location = 6) in vec3 inViewDirection;

//------------------------------------------------------------------------------
// Fragment Outputs
//------------------------------------------------------------------------------

layout(location = 0) out vec4 outColor;

//------------------------------------------------------------------------------
// Normal Mapping
//------------------------------------------------------------------------------

vec3 getNormalFromMap() {
    // Sample normal map (tangent space normal)
    vec3 tangentNormal = texture(NormalMap, inTexCoord0).xyz * 2.0 - 1.0;
    
    // Build TBN matrix
    vec3 N = normalize(inWorldNormal);
    vec3 T = normalize(inWorldTangent.xyz);
    
    // Re-orthogonalize T with respect to N (Gram-Schmidt)
    T = normalize(T - dot(T, N) * N);
    
    // Compute bitangent
    vec3 B = cross(N, T) * inWorldTangent.w;
    
    // TBN matrix transforms from tangent space to world space
    mat3 TBN = mat3(T, B, N);
    
    return normalize(TBN * tangentNormal);
}

//------------------------------------------------------------------------------
// Material Evaluation
//------------------------------------------------------------------------------

struct MaterialParams {
    vec3 baseColor;
    float metallic;
    float roughness;
    float reflectance;
    float ao;
    vec3 emissive;
    vec3 normal;
};

MaterialParams evaluateMaterial() {
    MaterialParams params;
    
    // Sample base color
    vec4 baseColorSample = texture(BaseColorMap, inTexCoord0);
    params.baseColor = baseColorSample.rgb * Material.BaseColorFactor.rgb * inVertexColor.rgb;
    
    // Sample metallic-roughness (glTF convention: G = roughness, B = metallic)
    vec4 mrSample = texture(MetallicRoughnessMap, inTexCoord0);
    params.roughness = mrSample.g * Material.RoughnessFactor;
    params.metallic = mrSample.b * Material.MetallicFactor;
    
    // Clamp roughness to avoid numerical issues
    params.roughness = max(params.roughness, MIN_ROUGHNESS);
    
    // Reflectance for dielectrics
    params.reflectance = Material.ReflectanceFactor;
    
    // Sample ambient occlusion
    params.ao = texture(OcclusionMap, inTexCoord0).r * Material.AmbientOcclusion;
    
    // Sample emissive
    params.emissive = texture(EmissiveMap, inTexCoord0).rgb * Material.EmissiveFactor.rgb;
    
    // Get normal from normal map
    params.normal = getNormalFromMap();
    
    return params;
}

//------------------------------------------------------------------------------
// Lighting Calculation
//------------------------------------------------------------------------------

// Compute F0 (specular reflectance at normal incidence)
vec3 computeF0(vec3 baseColor, float metallic, float reflectance) {
    vec3 dielectricF0 = vec3(0.16 * reflectance * reflectance);
    return mix(dielectricF0, baseColor, metallic);
}

// Compute diffuse color (metals have no diffuse)
vec3 computeDiffuseColor(vec3 baseColor, float metallic) {
    return baseColor * (1.0 - metallic);
}

// Evaluate direct lighting from a single light
vec3 evaluateDirectLight(
    vec3 N,           // Surface normal
    vec3 V,           // View direction
    vec3 L,           // Light direction
    vec3 lightColor,  // Light color * intensity
    vec3 diffuseColor,
    vec3 f0,
    float roughness
) {
    float NoL = saturate(dot(N, L));
    if (NoL <= 0.0) return vec3(0.0);
    
    float NoV = max(dot(N, V), MIN_N_DOT_V);
    
    // Half vector
    vec3 H = normalize(V + L);
    float NoH = saturate(dot(N, H));
    float LoH = saturate(dot(L, H));
    
    // Roughness to alpha
    float alpha = roughness * roughness;
    
    // Specular BRDF
    float D = D_GGX(alpha, NoH);
    float Vis = V_SmithGGXCorrelated(alpha, NoV, NoL);
    vec3 F = F_Schlick(f0, LoH);
    vec3 specular = D * Vis * F;
    
    // Diffuse BRDF
    vec3 diffuse = diffuseColor * Fd_Lambert();
    
    // Energy conservation: diffuse is reduced by Fresnel
    vec3 kD = (1.0 - F) * (1.0 - f0.r); // Approximate energy conservation
    
    // Final contribution
    return (kD * diffuse + specular) * lightColor * NoL;
}

// Light attenuation for point/spot lights
float getDistanceAttenuation(float distance, float range) {
    float d2 = distance * distance;
    float rangeAttenuation = sq(saturate(1.0 - sq(distance / range)));
    return rangeAttenuation / (d2 + 1.0);
}

// Spot light cone attenuation
float getSpotAttenuation(vec3 L, vec3 spotDir, float innerCone, float outerCone) {
    float cosAngle = dot(-L, spotDir);
    float attenuation = saturate((cosAngle - outerCone) / (innerCone - outerCone));
    return attenuation * attenuation;
}

//------------------------------------------------------------------------------
// Main
//------------------------------------------------------------------------------

void main() {
    // Evaluate material parameters
    MaterialParams mat = evaluateMaterial();
    
    // Compute derived material properties
    vec3 diffuseColor = computeDiffuseColor(mat.baseColor, mat.metallic);
    vec3 f0 = computeF0(mat.baseColor, mat.metallic, mat.reflectance);
    
    // Surface normal and view direction
    vec3 N = mat.normal;
    vec3 V = normalize(inViewDirection);
    
    // Accumulate lighting
    vec3 Lo = vec3(0.0);
    
    // Process each light
    for (int i = 0; i < Lighting.NumLights && i < MAX_LIGHTS; ++i) {
        LightData light = Lighting.Lights[i];
        int lightType = int(light.Position.w);
        
        vec3 L;
        float attenuation = 1.0;
        
        if (lightType == LIGHT_TYPE_DIRECTIONAL) {
            // Directional light: position stores direction
            L = normalize(-light.Position.xyz);
        }
        else if (lightType == LIGHT_TYPE_POINT) {
            // Point light
            vec3 lightVec = light.Position.xyz - inWorldPosition;
            float distance = length(lightVec);
            L = lightVec / distance;
            attenuation = getDistanceAttenuation(distance, light.Attenuation.x);
        }
        else if (lightType == LIGHT_TYPE_SPOT) {
            // Spot light
            vec3 lightVec = light.Position.xyz - inWorldPosition;
            float distance = length(lightVec);
            L = lightVec / distance;
            attenuation = getDistanceAttenuation(distance, light.Attenuation.x);
            attenuation *= getSpotAttenuation(L, normalize(light.Direction.xyz),
                                              light.Attenuation.y, light.Attenuation.z);
        }
        
        vec3 lightColor = light.Color.rgb * light.Color.w * attenuation;
        Lo += evaluateDirectLight(N, V, L, lightColor, diffuseColor, f0, mat.roughness);
    }
    
    // Simple ambient lighting (placeholder for IBL)
    vec3 ambient = diffuseColor * Lighting.AmbientIntensity * mat.ao;
    
    // Add emissive
    vec3 emissive = mat.emissive;
    
    // Final color
    vec3 color = Lo + ambient + emissive;
    
    // Tone mapping (simple Reinhard)
    color = color / (color + vec3(1.0));
    
    // Gamma correction (linear to sRGB)
    color = pow(color, vec3(1.0 / 2.2));
    
    outColor = vec4(color, 1.0);
}
